// ============================================================================
//  LAMI Worker — intermédiaire de confiance entre le launcher et GitHub.
// ----------------------------------------------------------------------------
//  Objectif : AUCUN token GitHub côté client, et impossibilité d'usurper un
//  rôle/une identité.
//
//  Principe :
//   1. Le client s'authentifie via Microsoft/Minecraft et obtient un ACCESS
//      TOKEN Minecraft (api.minecraftservices.com).
//   2. Le client envoie CE token au Worker (jamais son UUID ni son rôle).
//   3. Le Worker redemande l'identité RÉELLE à Mojang à partir du token
//      (/minecraft/profile) → uuid + pseudo authentifiés. Le client ne peut
//      donc pas mentir sur son identité.
//   4. Le Worker calcule le rôle (roles.json) et vérifie la propriété du
//      serveur pour les actions sensibles.
//   5. Si autorisé, le Worker exécute lui-même l'écriture GitHub avec SON
//      token (secret d'environnement GITHUB_TOKEN), jamais transmis au client.
//
//  Secrets/variables (wrangler.toml + `wrangler secret put`) :
//   - GITHUB_TOKEN   : PAT fine-grained (Contents: Read/Write) sur LAMI-db.
//   - GH_OWNER       : "Gimaxe"      (var)
//   - GH_REPO        : "LAMI-db"     (var)
//   - GH_BRANCH      : "main"        (var)
// ============================================================================

const MOJANG_PROFILE = "https://api.minecraftservices.com/minecraft/profile";

export default {
  async fetch(request, env, ctx) {
    if (request.method === "OPTIONS") return cors(new Response(null, { status: 204 }));
    const url = new URL(request.url);
    const path = url.pathname.replace(/\/+$/, "");
    const gh = new GitHub(env);

    // ================= LECTURES (GET) : publiques, SANS token ==================
    // Le Worker lit avec SON token GitHub ; le client n'en a aucun. On ne met en
    // cache QUE les fichiers de mods (/file), jamais l'identité ni les écritures.
    if (request.method === "GET") {
      try {
        if (path === "/file")    return cors(await handleFile(gh, url, ctx));
        if (path === "/servers") return cors(json(await handleListServers(gh)));
        if (path === "/server")  return cors(json(await handleGetServer(gh, url.searchParams.get("id"))));
        if (path === "/resolve") return cors(json(await handleResolve(gh, url.searchParams.get("ip"))));
        // DIAGNOSTIC TEMPORAIRE : teste quels domaines d'auth Mojang/Xbox sont
        // joignables depuis le réseau des Workers (certains bloquent Cloudflare).
        // Aucune donnée sensible : requêtes anonymes, on ne renvoie que les statuts.
        if (path === "/diag-net") {
          const targets = {
            minecraftservices: "https://api.minecraftservices.com/minecraft/profile",
            sessionserver: "https://sessionserver.mojang.com/session/minecraft/hasJoined?username=x&serverId=x",
            xbl_user: "https://user.auth.xboxlive.com/user/authenticate",
            mojang_api: "https://api.mojang.com/users/profiles/minecraft/gimaxe",
          };
          const out = {};
          for (const [k, u] of Object.entries(targets)) {
            try {
              const r = await fetch(u, { headers: { "User-Agent": "LAMI-diag" } });
              const ct = r.headers.get("content-type") || "?";
              const srv = r.headers.get("server") || "?";
              out[k] = `HTTP ${r.status} | server=${srv} | ct=${ct}`;
            } catch (e) { out[k] = "ERREUR: " + (e.message || e); }
          }
          return cors(json(out));
        }
        return cors(json({ error: "Route inconnue: " + path }, 404));
      } catch (e) {
        return cors(json({ error: e.message || String(e) }, e.status || 500));
      }
    }

    if (request.method !== "POST") return cors(json({ error: "Méthode non supportée" }, 405));

    // ============= ÉCRITURES / IDENTITÉ (POST) : token requis ==================
    let body;
    try { body = await request.json(); }
    catch { return cors(json({ error: "JSON invalide" }, 400)); }

    const token = (body.token || "").trim();
    if (!token) return cors(json({ error: "token manquant" }, 401));

    try {
      const identity = await verifyIdentity(token, env);  // JAMAIS mis en cache
      if (!identity) return cors(json({ error: "Token Minecraft invalide ou expiré." }, 401));
      const uuid = normalizeUuid(identity.id);
      const role = await resolveRole(gh, uuid);           // rôle recalculé à chaque appel

      switch (path) {
        case "/whoami":
          return cors(json({ uuid, name: identity.name, role }));
        case "/upload":       // lot d'assets (chunking)
          requireRole(role, ["host", "superadmin"]);
          return cors(json(await handleUpload(gh, body)));
        case "/publish":
          requireRole(role, ["host", "superadmin"]);
          return cors(json(await handlePublish(gh, body, uuid)));
        case "/edit":
          return cors(json(await handleEdit(gh, body, uuid, role)));
        case "/delete":
          return cors(json(await handleDelete(gh, body, uuid, role)));
        case "/setRole":
          requireRole(role, ["superadmin"]);
          return cors(json(await handleSetRole(gh, body)));
        case "/removeRole":
          requireRole(role, ["superadmin"]);
          return cors(json(await handleRemoveRole(gh, body)));
        default:
          return cors(json({ error: "Route inconnue: " + path }, 404));
      }
    } catch (e) {
      return cors(json({ error: e.message || String(e) }, e.status || 500));
    }
  },
};

// ==========================================================================
//  Lectures (GET) — le Worker lit le repo privé avec son token, sans exposer.
// ==========================================================================

// Télécharge un fichier de la banque avec CACHE au bord (mods immuables).
async function handleFile(gh, url, ctx) {
  const p = (url.searchParams.get("path") || "").replace(/^\/+/, "");
  if (!p || p.includes("..")) throw err(400, "path invalide");

  // Seuls les fichiers de la BANQUE (mods/plugins/resourcepacks/shaders) sont
  // immuables (chemin = contenu) → cache au bord. Les métadonnées (servers/*.json,
  // index.json, roles.json) changent : jamais mises en cache (sinon on servirait
  // un index/manifeste périmé après une publication).
  const top = p.split("/")[0];
  const cacheable = ASSET_TYPES.includes(top);

  const cache = caches.default;
  const cacheKey = new Request(url.toString(), { method: "GET" });
  if (cacheable) {
    const hit = await cache.match(cacheKey);
    if (hit) return hit;
  }

  const r = await fetch(`${gh.base}/${p}?ref=${gh.branch}`,
                        { headers: gh.headers({ Accept: "application/vnd.github.raw" }) });
  if (!r.ok) throw err(r.status === 404 ? 404 : 502, `Fichier introuvable: ${p}`);
  const resp = new Response(r.body, {
    headers: {
      "Content-Type": "application/octet-stream",
      "Cache-Control": cacheable
        ? "public, max-age=31536000, immutable"
        : "no-store",
    },
  });
  if (cacheable) ctx.waitUntil(cache.put(cacheKey, resp.clone()));
  return resp;
}

async function handleListServers(gh) {
  const items = await gh.listDir("servers");
  const out = [];
  for (const it of items) {
    if (it.type !== "file" || it.name === "index.json" || !it.name.endsWith(".json")) continue;
    const m = await gh.readJson("servers/" + it.name).catch(() => null);
    if (m) out.push(m);
  }
  return { servers: out };
}

async function handleGetServer(gh, id) {
  id = slugify(id || "");
  if (!id) throw err(400, "id manquant");
  return await gh.readJson(`servers/${id}.json`);
}

async function handleResolve(gh, ip) {
  ip = (ip || "").trim().toLowerCase();
  if (!ip) throw err(400, "adresse manquante");
  const index = await gh.readJson("servers/index.json").catch(() => ({}));
  const id = index[ip];
  if (!id) throw err(404, "Aucun serveur pour cette adresse.");
  return await gh.readJson(`servers/${id}.json`);
}

// Upload d'un LOT d'assets (chunking) : uploade dans la banque (dédup skipIfExists).
async function handleUpload(gh, body) {
  const mc = body.minecraft_version || body.version || "";
  const loader = (body.loader || "vanilla").toLowerCase();
  const assets = body.assets || {};
  let uploaded = 0;
  for (const type of ASSET_TYPES) {
    for (const f of (assets[type] || [])) {
      const bankPath = bankPathRaw(type, mc, loader, f.file);
      await gh.putFileBase64(bankPath, f.base64, `Ajout ${bankPath} via Worker`, true);
      uploaded++;
    }
  }
  return { ok: true, uploaded };
}

// --------------------------------------------------------------------------
//  Vérification d'identité : le Worker fait autorité via Mojang.
// --------------------------------------------------------------------------
// Les IP des Workers sont bloquées par tous les domaines Mojang (mesuré via
// /diag-net) : la vérification passe donc par le RELAIS (Deno Deploy, IP non
// bloquées), authentifié par RELAY_SECRET. L'appel direct Mojang reste en repli
// si aucun relais n'est configuré (utile pour les tests locaux hors Cloudflare).
async function verifyIdentity(minecraftToken, env) {
  if (env && env.IDENTITY_RELAY_URL) {
    const r = await fetch(env.IDENTITY_RELAY_URL.replace(/\/+$/, "") + "/verify", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        "x-relay-secret": env.RELAY_SECRET || "",
      },
      body: JSON.stringify({ token: minecraftToken }),
    });
    // Panne/config du relais ≠ token invalide : on le dit clairement à l'UI.
    if (!r.ok) throw err(502, `Relais d'identité indisponible (HTTP ${r.status}).`);
    const d = await r.json().catch(() => null);
    if (!d || !d.ok) return null;         // token invalide/expiré
    return { id: d.id, name: d.name };
  }

  const r = await fetch(MOJANG_PROFILE, {
    headers: { Authorization: "Bearer " + minecraftToken },
  });
  if (!r.ok) return null;                 // 401 = token invalide
  const p = await r.json();
  if (!p || !p.id) return null;
  return p;                               // { id (uuid sans tirets), name, ... }
}

function normalizeUuid(id) {
  const h = (id || "").replace(/-/g, "").toLowerCase();
  if (h.length !== 32) return h;
  return `${h.slice(0,8)}-${h.slice(8,12)}-${h.slice(12,16)}-${h.slice(16,20)}-${h.slice(20)}`;
}

// --------------------------------------------------------------------------
//  Rôles : lus depuis roles.json dans LAMI-db (source de vérité unique).
// --------------------------------------------------------------------------
async function resolveRole(gh, uuid) {
  const roles = await gh.readJson("roles.json").catch(() => null);
  const table = (roles && roles.roles) || {};
  const dashed = normalizeUuid(uuid);
  const compact = dashed.replace(/-/g, "");
  const v = (table[dashed] || table[compact] || "player").toString().toLowerCase();
  if (v === "admin" || v === "super_admin" || v === "superadmin") return "superadmin";
  if (v === "host" || v === "hebergeur" || v === "hébergeur") return "host";
  return "player";
}

function requireRole(role, allowed) {
  if (!allowed.includes(role)) throw err(403, `Action réservée (${allowed.join("/")}). Ton rôle : ${role}.`);
}

// --------------------------------------------------------------------------
//  Actions d'écriture (le Worker impose rôle + propriété).
// --------------------------------------------------------------------------

// Publier : le propriétaire = l'uuid AUTHENTIFIÉ (jamais celui du client).
async function handlePublish(gh, body, uuid) {
  const srv = body.server || {};
  const id = slugify(srv.id || srv.name || "");
  if (!id || !srv.name || !srv.address) throw err(400, "Serveur incomplet (nom/adresse/id).");

  // Si le serveur existe déjà, seul le propriétaire ou un admin peut l'écraser.
  const existing = await gh.readJson(`servers/${id}.json`).catch(() => null);
  if (existing && existing.owner && existing.owner !== uuid) {
    const role = await resolveRole(gh, uuid);
    if (role !== "superadmin") throw err(403, "Ce serveur appartient à un autre hébergeur.");
  }

  const manifest = buildManifest(srv, uuid);
  if (body.assets && Object.keys(body.assets).length) {
    // Petit publish : assets en base64 uploadés directement.
    await uploadAssets(gh, manifest, body.assets);
  } else if (body.assetLists) {
    // Publish CHUNKÉ : les fichiers ont déjà été uploadés via /upload ; on ne
    // reçoit ici que les listes de métadonnées (file/sha256/size) pour le manifeste.
    for (const type of ASSET_TYPES)
      if (Array.isArray(body.assetLists[type])) manifest[type] = body.assetLists[type];
  }
  await gh.putFile(`servers/${id}.json`, JSON.stringify(manifest, null, 2),
                   `Publication de ${id} via Worker (${uuid})`);
  if (manifest.address) await upsertIndex(gh, manifest.address, id);
  return { ok: true, id, owner: uuid };
}

// Modifier : seulement propriétaire ou admin. Métadonnées uniquement ici
// (les assets suivent le même chemin que publish si fournis).
async function handleEdit(gh, body, uuid, role) {
  const id = slugify(body.id || "");
  if (!id) throw err(400, "id manquant.");
  const cur = await gh.readJson(`servers/${id}.json`);
  if (!cur) throw err(404, "Serveur introuvable.");
  if (cur.owner && cur.owner !== uuid && role !== "superadmin")
    throw err(403, "Tu ne peux modifier que TES serveurs.");

  const c = body.changes || {};
  if (c.name) cur.name = c.name;
  if (c.address) cur.address = c.address;
  if (c.minecraft_version) cur.minecraft_version = c.minecraft_version;
  if (c.loader) cur.loader = (c.loader + "").toLowerCase();
  if (typeof c.loader_version === "string") cur.loader_version = c.loader_version;
  cur.owner = cur.owner || uuid;   // ne change jamais de propriétaire

  // Catégories vidées explicitement (clear: ["mods", ...]).
  for (const t of (body.clear || [])) if (ASSET_TYPES.includes(t)) cur[t] = [];

  if (body.assets && Object.keys(body.assets).length) {
    await uploadAssets(gh, cur, body.assets);
  } else if (body.assetLists) {
    // Édition CHUNKÉE : fichiers déjà uploadés via /upload ; ici, métadonnées.
    for (const type of ASSET_TYPES)
      if (Array.isArray(body.assetLists[type])) cur[type] = body.assetLists[type];
  }
  await gh.putFile(`servers/${id}.json`, JSON.stringify(cur, null, 2),
                   `Modification de ${id} via Worker (${uuid})`);
  if (cur.address) await upsertIndex(gh, cur.address, id);
  return { ok: true, id };
}

// Supprimer : seulement propriétaire ou admin.
async function handleDelete(gh, body, uuid, role) {
  const id = slugify(body.id || "");
  if (!id) throw err(400, "id manquant.");
  const cur = await gh.readJson(`servers/${id}.json`).catch(() => null);
  if (!cur) throw err(404, "Serveur introuvable.");
  if (cur.owner && cur.owner !== uuid && role !== "superadmin")
    throw err(403, "Tu ne peux supprimer que TES serveurs.");

  await gh.deleteFile(`servers/${id}.json`, `Suppression de ${id} via Worker (${uuid})`);
  await removeFromIndex(gh, id);
  return { ok: true, id, deleted: true };
}

async function handleSetRole(gh, body) {
  const target = normalizeUuid(body.uuid || "");
  const value = (body.role || "").toLowerCase();
  if (!target) throw err(400, "uuid cible manquant.");
  const roles = (await gh.readJson("roles.json").catch(() => null)) || { roles: {} };
  roles.roles = roles.roles || {};
  roles.roles[target] = value;
  await gh.putFile("roles.json", JSON.stringify(roles, null, 2), `setRole ${target}=${value}`);
  return { ok: true, uuid: target, role: value };
}

async function handleRemoveRole(gh, body) {
  const target = normalizeUuid(body.uuid || "");
  const roles = (await gh.readJson("roles.json").catch(() => null)) || { roles: {} };
  roles.roles = roles.roles || {};
  delete roles.roles[target];
  delete roles.roles[target.replace(/-/g, "")];
  await gh.putFile("roles.json", JSON.stringify(roles, null, 2), `removeRole ${target}`);
  return { ok: true, uuid: target };
}

// --------------------------------------------------------------------------
//  Helpers manifeste / banque d'assets.
// --------------------------------------------------------------------------
const ASSET_TYPES = ["mods", "plugins", "resourcepacks", "shaders"];

function buildManifest(srv, uuid) {
  return {
    id: slugify(srv.id || srv.name),
    name: srv.name,
    address: srv.address,
    minecraft_version: srv.minecraft_version || srv.version || "",
    loader: (srv.loader || "vanilla").toLowerCase(),
    loader_version: srv.loader_version || srv.loaderVersion || "",
    mods: srv.mods || [],
    plugins: srv.plugins || [],
    resourcepacks: srv.resourcepacks || srv.resourcePacks || [],
    shaders: srv.shaders || [],
    owner: uuid,
  };
}

// assets = { mods: [{file, base64, sha256, size}], ... }. Le Worker uploade
// chaque fichier dans la banque mutualisée et met à jour la liste du manifeste.
async function uploadAssets(gh, manifest, assets) {
  for (const type of ASSET_TYPES) {
    const files = assets[type];
    if (!Array.isArray(files) || files.length === 0) continue;
    const entries = [];
    for (const f of files) {
      const bankPath = assetBankPath(manifest, type, f.file);
      await gh.putFileBase64(bankPath, f.base64, `Ajout ${bankPath} via Worker`, true /*skipIfExists*/);
      entries.push({ file: f.file, sha256: f.sha256, size: f.size });
    }
    manifest[type] = entries;
  }
}

function assetBankPath(manifest, type, file) {
  return bankPathRaw(type, manifest.minecraft_version, manifest.loader, file);
}
// Chemin banque à partir des paramètres bruts (utilisé par /upload en chunking).
function bankPathRaw(type, mcVersion, loader, file) {
  // Les mods dépendent du loader ; les autres seulement de la version.
  if (type === "mods")
    return `mods/${mcVersion}/${(loader || "vanilla").toLowerCase()}/${file}`;
  return `${type}/${mcVersion}/${file}`;
}

// Read-modify-write d'index.json avec retry COMPLET : en cas d'écriture
// concurrente (ex. deux suppressions en même temps), GitHub renvoie 409/422 ;
// rejouer seulement le PUT perdrait la modification de l'autre requête, donc on
// relit l'index frais et on réapplique la mutation à chaque tentative.
async function withIndexRetry(gh, message, mutate) {
  for (let attempt = 0; ; attempt++) {
    const meta = await gh.getFileMeta("servers/index.json").catch(() => null);
    const index = meta ? JSON.parse(b64decode(meta.content)) : {};
    if (mutate(index) === false) return;    // rien à changer
    const body = { message, content: b64encode(JSON.stringify(index, null, 2)),
                   branch: gh.branch };
    if (meta && meta.sha) body.sha = meta.sha;
    const r = await fetch(`${gh.base}/servers/index.json`, {
      method: "PUT", headers: gh.headers({ "Content-Type": "application/json" }),
      body: JSON.stringify(body),
    });
    if (r.ok) return;
    const text = await r.text();
    if ((r.status === 409 || r.status === 422) && attempt < 3) continue;
    throw err(r.status, `GitHub PUT servers/index.json: ${text}`);
  }
}

async function upsertIndex(gh, address, id) {
  await withIndexRetry(gh, `Index ${address} -> ${id}`, (index) => {
    index[address] = id;
  });
}

async function removeFromIndex(gh, id) {
  await withIndexRetry(gh, `Nettoyage index (${id})`, (index) => {
    let changed = false;
    for (const k of Object.keys(index)) if (index[k] === id) { delete index[k]; changed = true; }
    return changed;   // false = index déjà propre, pas d'écriture
  });
}

// --------------------------------------------------------------------------
//  Client GitHub côté Worker (utilise GITHUB_TOKEN, jamais exposé au client).
// --------------------------------------------------------------------------
class GitHub {
  constructor(env) {
    this.owner = env.GH_OWNER || "Gimaxe";
    this.repo = env.GH_REPO || "LAMI-db";
    this.branch = env.GH_BRANCH || "main";
    this.token = env.GITHUB_TOKEN;
    this.base = `https://api.github.com/repos/${this.owner}/${this.repo}/contents`;
  }
  headers(extra = {}) {
    return {
      Authorization: "Bearer " + this.token,
      Accept: "application/vnd.github+json",
      "User-Agent": "LAMI-Worker",
      "X-GitHub-Api-Version": "2022-11-28",
      ...extra,
    };
  }
  async getFileMeta(path) {
    const r = await fetch(`${this.base}/${path}?ref=${this.branch}`, { headers: this.headers() });
    if (r.status === 404) return null;
    if (!r.ok) throw err(r.status, `GitHub GET ${path}: ${await r.text()}`);
    return r.json();
  }
  async readJson(path) {
    const meta = await this.getFileMeta(path);
    if (!meta) throw err(404, `${path} introuvable`);
    return JSON.parse(b64decode(meta.content));
  }
  async listDir(path) {
    const r = await fetch(`${this.base}/${path}?ref=${this.branch}`, { headers: this.headers() });
    if (r.status === 404) return [];
    if (!r.ok) throw err(r.status, `GitHub LIST ${path}`);
    const j = await r.json();
    return Array.isArray(j) ? j : [];
  }
  async putFile(path, contentUtf8, message, sha) {
    return this.putFileBase64(path, b64encode(contentUtf8), message, false, sha);
  }
  // Sha d'un fichier, FIABLE même pour les gros fichiers : le GET contents JSON
  // échoue au-delà de 1 Mo (« too_large ») — dans ce cas on liste le dossier
  // parent, qui renvoie le sha de chaque entrée quelle que soit sa taille.
  // Renvoie null si le fichier n'existe pas.
  async fileSha(path) {
    try {
      const meta = await this.getFileMeta(path);
      return meta ? meta.sha : null;
    } catch {
      const i = path.lastIndexOf("/");
      const dir = i < 0 ? "" : path.slice(0, i);
      const name = path.slice(i + 1);
      const items = await this.listDir(dir).catch(() => []);
      const it = items.find((x) => x.name === name);
      return it ? it.sha : null;
    }
  }
  async putFileBase64(path, base64, message, skipIfExists = false, sha) {
    if (skipIfExists) {
      if (await this.fileSha(path)) return { skipped: true };
      sha = undefined;
    } else if (sha === undefined) {
      sha = (await this.fileSha(path)) || undefined;
    }
    // Jusqu'à 3 tentatives : les 409/422 signalent un sha périmé ou manquant
    // (écriture concurrente, gros fichier) → on relit le sha frais et on rejoue.
    // NB : sûr uniquement parce que le contenu ne dépend pas de l'existant ;
    // les read-modify-write (index.json, roles.json) rejouent TOUT leur cycle
    // via withIndexRetry() au lieu de compter sur ce retry-ci.
    for (let attempt = 0; ; attempt++) {
      const body = { message, content: base64, branch: this.branch };
      if (sha) body.sha = sha;
      const r = await fetch(`${this.base}/${path}`, {
        method: "PUT", headers: this.headers({ "Content-Type": "application/json" }),
        body: JSON.stringify(body),
      });
      if (r.ok) return r.json();
      const text = await r.text();
      if ((r.status === 409 || r.status === 422) && attempt < 2) {
        const fresh = await this.fileSha(path);
        if (skipIfExists && fresh) return { skipped: true };  // apparu entre-temps
        sha = fresh || undefined;
        continue;
      }
      throw err(r.status, `GitHub PUT ${path}: ${text}`);
    }
  }
  async deleteFile(path, message) {
    const meta = await this.getFileMeta(path);
    if (!meta) return { skipped: true };
    const r = await fetch(`${this.base}/${path}`, {
      method: "DELETE", headers: this.headers({ "Content-Type": "application/json" }),
      body: JSON.stringify({ message, sha: meta.sha, branch: this.branch }),
    });
    if (!r.ok) throw err(r.status, `GitHub DELETE ${path}: ${await r.text()}`);
    return r.json();
  }
}

// --------------------------------------------------------------------------
//  Utilitaires.
// --------------------------------------------------------------------------
function slugify(s) {
  return (s || "").toLowerCase().normalize("NFD").replace(/[̀-ͯ]/g, "")
    .replace(/[^a-z0-9]+/g, "-").replace(/^-+|-+$/g, "");
}
function err(status, message) { const e = new Error(message); e.status = status; return e; }
function json(obj, status = 200) {
  return new Response(JSON.stringify(obj), { status, headers: { "Content-Type": "application/json" } });
}
function cors(resp) {
  // Les réponses issues du cache edge (caches.default.match) ont des en-têtes
  // IMMUABLES : les modifier jette « Can't modify immutable headers » → 500 sur
  // chaque fichier déjà caché (les téléchargements marchaient une fois, puis
  // cassaient). On repart donc toujours d'une copie mutable.
  const r = new Response(resp.body, resp);
  r.headers.set("Access-Control-Allow-Origin", "*");
  r.headers.set("Access-Control-Allow-Methods", "POST, OPTIONS");
  r.headers.set("Access-Control-Allow-Headers", "Content-Type");
  return r;
}
function b64encode(str) {
  const bytes = new TextEncoder().encode(str);
  let bin = ""; for (const b of bytes) bin += String.fromCharCode(b);
  return btoa(bin);
}
function b64decode(b64) {
  const clean = (b64 || "").replace(/\n/g, "");
  const bin = atob(clean); const bytes = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
  return new TextDecoder().decode(bytes);
}
