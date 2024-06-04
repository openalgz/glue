var incppect = {
    // websocket data
    ws: null,

    // default ws url - change to fit your needs
    ws_uri: 'ws://' + window.location.hostname + ':' + window.location.port + '/incppect',

    // vars data
    vars_map: {},
    last_data: null,

    // requests data
    requests: [],
    requests_old: [],
    requests_new_vars: false,
    requests_regenerate: true,

    // timestamps
    t_start_ms: null,
    t_frame_begin_ms: null,
    t_requests_last_update_ms: null,

    // constants
    k_auto_reconnect: true,
    k_requests_update_freq_ms: 50,

    // stats
    stats: {
        tx_n: 0,
        tx_bytes: 0,

        rx_n: 0,
        rx_bytes: 0,
    },

    timestamp: function () {
        return window.performance && window.performance.now && window.performance.timing &&
            window.performance.timing.navigationStart ? window.performance.now() + window.performance.timing.navigationStart : Date.now();
    },

    init: function () {
        var onopen = this.onopen.bind(this);
        var onclose = this.onclose.bind(this);
        var onmessage = this.onmessage.bind(this);
        var onerror = this.onerror.bind(this);

        this.ws = new WebSocket(this.ws_uri);
        this.ws.binaryType = 'arraybuffer';
        this.ws.onopen = function (evt) { onopen(evt) };
        this.ws.onclose = function (evt) { onclose(evt) };
        this.ws.onmessage = function (evt) { onmessage(evt) };
        this.ws.onerror = function (evt) { onerror(evt) };

        this.t_start_ms = this.timestamp();
        this.t_requests_last_update_ms = this.timestamp() - this.k_requests_update_freq_ms;

        window.requestAnimationFrame(this.loop.bind(this));
    },

    loop: function () {
        if (this.ws == null) {
            if (this.k_auto_reconnect) {
                setTimeout(this.init.bind(this), 1000);
            }
            return;
        }

        if (this.ws.readyState !== this.ws.OPEN) {
            window.requestAnimationFrame(this.loop.bind(this));
            return;
        }

        this.t_frame_begin_ms = this.timestamp();
        this.requests_regenerate = this.t_frame_begin_ms - this.t_requests_last_update_ms > this.k_requests_update_freq_ms;

        if (this.requests_regenerate) {
            this.requests_old = this.requests;
            this.requests = [];
        }

        try {
            this.render();
        } catch (err) {
            this.onerror('Failed to render state: ' + err);
        }

        if (this.requests_regenerate) {
            if (this.requests_new_vars) {
                this.send_var_to_id_map();
                this.requests_new_vars = false;
            }
            this.send_requests();
            this.t_requests_last_update_ms = this.timestamp();
        }

        window.requestAnimationFrame(this.loop.bind(this));
    },

    get: function (path) {
        if (!(path in this.vars_map)) {
            this.vars_map[path] = new Uint8Array();
            this.requests_new_vars = true;
        }

        if (this.requests_regenerate) {
            this.requests.push(path);
        }

        return this.vars_map[path];
    },

    send: function (msg) {
        var enc_msg = new TextEncoder().encode(msg);
        var data = write_beve([4, enc_msg]);
        this.ws.send(data);

        this.stats.tx_n += 1;
        this.stats.tx_bytes += data.length;
    },

    send_var_to_id_map: function () {
        var data = write_beve([1, Object.keys(vars_map)]);
        this.ws.send(data);

        this.stats.tx_n += 1;
        this.stats.tx_bytes += data.length;
    },

    send_requests: function () {
        var same = true;
        if (this.requests_old === null || this.requests.length !== this.requests_old.length) {
            same = false;
        } else {
            for (var i = 0; i < this.requests.length; ++i) {
                if (this.requests[i] !== this.requests_old[i]) {
                    same = false;
                    break;
                }
            }
        }

        // If the request buffer is the same, don't resend it, just send a message saying use the old request
        if (same) {
            var data = write_beve([3]);
            this.ws.send(data);

            this.stats.tx_n += 1;
            this.stats.tx_bytes += data.length;
        } else {
            var data = write_beve([2, this.requests]);
            this.ws.send(data);

            this.stats.tx_n += 1;
            this.stats.tx_bytes += data.length;
        }
    },

    onopen: function (evt) {
    },

    onclose: function (evt) {
        this.vars_map = {};
        this.requests = null;
        this.requests_old = null;
        this.ws = null;
    },

    // on receive
    // evt input is from websocket
    onmessage: function (evt) {
        this.stats.rx_n += 1;
        this.stats.rx_bytes += evt.data.byteLength;

        var type_all = (new Uint32Array(evt.data))[0];

        // decompression
        if (this.last_data != null && type_all == 1) {
            var ntotal = evt.data.byteLength / 4 - 1;

            var src_view = new Uint32Array(evt.data, 4);
            var dst_view = new Uint32Array(this.last_data, 4);

            var k = 0;
            for (var i = 0; i < ntotal / 2; ++i) {
                var n = src_view[2 * i + 0];
                var c = src_view[2 * i + 1];
                for (var j = 0; j < n; ++j) {
                    dst_view[k] = dst_view[k] ^ c;
                    ++k;
                }
            }
        } else {
            this.last_data = evt.data;
        }

        var int_view = new Uint32Array(this.last_data); // TODO: don't copy
        var offset = 1;
        var offset_new = 0;
        var total_size = this.last_data.byteLength;
        var id = 0;
        var type = 0;
        var len = 0;
        while (4 * offset < total_size) {
            id = int_view[offset + 0];
            type = int_view[offset + 1];
            len = int_view[offset + 2];
            offset += 3;
            offset_new = offset + len / 4;
            if (type == 0) {
                this.vars_map[this.id_to_var[id]] = this.last_data.slice(4 * offset, 4 * offset_new);
            } else {
                var src_view = new Uint32Array(this.last_data, 4 * offset);
                var dst_view = new Uint32Array(this.vars_map[this.id_to_var[id]]);

                var k = 0;
                for (var i = 0; i < len / 8; ++i) {
                    var n = src_view[2 * i + 0];
                    var c = src_view[2 * i + 1];
                    for (var j = 0; j < n; ++j) {
                        dst_view[k] = dst_view[k] ^ c;
                        ++k;
                    }
                }
            }
            offset = offset_new;
        }
    },

    onerror: function (evt) {
        console.error("[incppect]", evt);
    },

    render: function () {
    },
}
