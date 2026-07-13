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
  async fetch(request, env) {
    // CORS simple (le launcher est une app native, mais utile en dev navigateur).
    if (request.method === "OPTIONS") return cors(new Response(null, { status: 204 }));
    if (request.method !== "POST") return cors(json({ error: "POST attendu" }, 405));

    const url = new URL(request.url);
    const path = url.pathname.replace(/\/+$/, "");

    let body;
    try { body = await request.json(); }
    catch { return cors(json({ error: "JSON invalide" }, 400)); }

    // --- 1) Identité vérifiée par Mojang (jamais fournie par le client) --------
    const token = (body.token || "").trim();
    if (!token) return cors(json({ error: "token manquant" }, 401));
    const identity = await verifyIdentity(token);
    if (!identity) return cors(json({ error: "Token Minecraft invalide ou expiré." }, 401));
    const uuid = normalizeUuid(identity.id);   // uuid AUTHENTIFIÉ
    const name = identity.name;

    const gh = new GitHub(env);
    const role = await resolveRole(gh, uuid);   // rôle RECALCULÉ côté serveur

    try {
      switch (path) {
        case "/whoami":
          return cors(json({ uuid, name, role }));

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
      const status = e.status || 500;
      return cors(json({ error: e.message || String(e) }, status));
    }
  },
};

// --------------------------------------------------------------------------
//  Vérification d'identité : le Worker fait autorité via Mojang.
// --------------------------------------------------------------------------
async function verifyIdentity(minecraftToken) {
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

  // Upload des assets fournis (base64) dans la banque, puis manifeste.
  const manifest = buildManifest(srv, uuid);
  await uploadAssets(gh, manifest, body.assets || {});
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

  await uploadAssets(gh, cur, body.assets || {});
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
  // Les mods dépendent du loader ; les autres seulement de la version.
  if (type === "mods")
    return `mods/${manifest.minecraft_version}/${manifest.loader}/${file}`;
  return `${type}/${manifest.minecraft_version}/${file}`;
}

async function upsertIndex(gh, address, id) {
  const meta = await gh.getFileMeta("servers/index.json").catch(() => null);
  const index = meta ? JSON.parse(b64decode(meta.content)) : {};
  index[address] = id;
  await gh.putFile("servers/index.json", JSON.stringify(index, null, 2),
                   `Index ${address} -> ${id}`, meta && meta.sha);
}

async function removeFromIndex(gh, id) {
  const meta = await gh.getFileMeta("servers/index.json").catch(() => null);
  if (!meta) return;
  const index = JSON.parse(b64decode(meta.content));
  for (const k of Object.keys(index)) if (index[k] === id) delete index[k];
  await gh.putFile("servers/index.json", JSON.stringify(index, null, 2),
                   `Nettoyage index (${id})`, meta.sha);
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
  async putFile(path, contentUtf8, message, sha) {
    return this.putFileBase64(path, b64encode(contentUtf8), message, false, sha);
  }
  async putFileBase64(path, base64, message, skipIfExists = false, sha) {
    if (skipIfExists) {
      const existing = await this.getFileMeta(path).catch(() => null);
      if (existing) return { skipped: true };
      sha = undefined;
    } else if (sha === undefined) {
      const existing = await this.getFileMeta(path).catch(() => null);
      sha = existing ? existing.sha : undefined;
    }
    const body = { message, content: base64, branch: this.branch };
    if (sha) body.sha = sha;
    const r = await fetch(`${this.base}/${path}`, {
      method: "PUT", headers: this.headers({ "Content-Type": "application/json" }),
      body: JSON.stringify(body),
    });
    if (!r.ok) throw err(r.status, `GitHub PUT ${path}: ${await r.text()}`);
    return r.json();
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
  resp.headers.set("Access-Control-Allow-Origin", "*");
  resp.headers.set("Access-Control-Allow-Methods", "POST, OPTIONS");
  resp.headers.set("Access-Control-Allow-Headers", "Content-Type");
  return resp;
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
