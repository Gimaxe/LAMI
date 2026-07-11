// Pont UI ↔ backend C++ via WebSocket local.
// Expose window.lami : { connected, ready(), call(method, params), on(event, cb) }.
// Le backend écoute sur ws://127.0.0.1:<window.LAMI_WS_PORT> (défaut 8770).
(function () {
    const port = window.LAMI_WS_PORT || 8770;
    let ws = null;
    let connected = false;
    let nextId = 1;
    const pending = new Map();     // id -> { resolve, reject }
    const listeners = new Map();   // event -> [cb]
    const readyWaiters = [];

    function connect() {
        try {
            ws = new WebSocket('ws://127.0.0.1:' + port);
        } catch (e) {
            setTimeout(connect, 1000);
            return;
        }

        ws.onopen = function () {
            connected = true;
            readyWaiters.splice(0).forEach(function (r) { r(); });
            document.dispatchEvent(new Event('lami-connected'));
        };
        ws.onclose = function () {
            connected = false;
            document.dispatchEvent(new Event('lami-disconnected'));
            setTimeout(connect, 1000); // reconnexion auto
        };
        ws.onerror = function () { try { ws.close(); } catch (e) {} };
        ws.onmessage = function (ev) {
            let msg;
            try { msg = JSON.parse(ev.data); } catch (e) { return; }

            // Événement poussé (progression, etc.)
            if (msg.event) {
                (listeners.get(msg.event) || []).forEach(function (cb) {
                    try { cb(msg); } catch (e) { console.error(e); }
                });
                return;
            }
            // Réponse à une requête
            if (msg.id != null && pending.has(msg.id)) {
                const p = pending.get(msg.id);
                pending.delete(msg.id);
                if (msg.ok) p.resolve(msg.result);
                else p.reject(new Error(msg.error || 'Erreur backend'));
            }
        };
    }

    connect();

    window.lami = {
        get connected() { return connected; },
        ready: function () {
            return connected ? Promise.resolve()
                             : new Promise(function (r) { readyWaiters.push(r); });
        },
        call: function (method, params) {
            params = params || {};
            return new Promise(function (resolve, reject) {
                if (!connected) { reject(new Error('Backend non connecté')); return; }
                const id = nextId++;
                pending.set(id, { resolve: resolve, reject: reject });
                ws.send(JSON.stringify({ id: id, method: method, params: params }));
                setTimeout(function () {
                    if (pending.has(id)) { pending.delete(id); reject(new Error('Délai dépassé')); }
                }, 30000);
            });
        },
        on: function (event, cb) {
            if (!listeners.has(event)) listeners.set(event, []);
            listeners.get(event).push(cb);
        },
    };
})();
