// LAMI — relais de vérification d'identité Minecraft (Deno Deploy).
//
// Pourquoi ce service existe : les IP de sortie des Cloudflare Workers sont
// bloquées par TOUS les domaines Mojang (minecraftservices, sessionserver,
// api.mojang.com — mesuré le 2026-07-26 via /diag-net). Le Worker délègue donc
// la seule question « ce token Minecraft est-il valide, et pour quel joueur ? »
// à ce relais, dont les IP (Deno Deploy) ne sont pas bloquées.
//
// Sécurité :
//  - RELAY_SECRET (variable d'environnement) : seul le Worker LAMI, qui connaît
//    ce secret, peut utiliser le relais. Sans lui → 401.
//  - Le relais ne détient AUCUN secret GitHub et ne peut rien écrire : il ne
//    fait que relayer une vérification de token vers Mojang.
//  - Réponses : { ok: true, id, name } si le token est valide,
//               { ok: false } si Mojang le rejette (token invalide/expiré).

const MOJANG_PROFILE = "https://api.minecraftservices.com/minecraft/profile";

Deno.serve(async (req: Request) => {
  const url = new URL(req.url);

  // Petit endpoint de vie (sans secret) pour vérifier que le relais tourne.
  if (req.method === "GET" && url.pathname === "/health")
    return Response.json({ ok: true, service: "lami-identity-relay" });

  if (req.method !== "POST" || url.pathname !== "/verify")
    return Response.json({ error: "POST /verify uniquement" }, { status: 404 });

  const secret = Deno.env.get("RELAY_SECRET") ?? "";
  if (!secret || req.headers.get("x-relay-secret") !== secret)
    return Response.json({ error: "non autorisé" }, { status: 401 });

  let body: { token?: string };
  try { body = await req.json(); }
  catch { return Response.json({ error: "JSON invalide" }, { status: 400 }); }

  const token = (body.token ?? "").trim();
  if (!token) return Response.json({ error: "token manquant" }, { status: 400 });

  const r = await fetch(MOJANG_PROFILE, {
    headers: { Authorization: "Bearer " + token },
  });

  // 401 Mojang = token invalide/expiré : réponse « propre » (HTTP 200, ok:false)
  // pour que le Worker distingue « token refusé » d'un « relais en panne ».
  if (!r.ok) return Response.json({ ok: false, status: r.status });

  const p = await r.json().catch(() => null);
  if (!p?.id) return Response.json({ ok: false, status: 401 });
  return Response.json({ ok: true, id: p.id, name: p.name });
});
