# Connect to a host Model with one Session

This client uses the [Telemetry UX WebSocket demo](https://github.com/shpegun60/telemetry-ux/tree/main/examples/web_demo)
as its explicitly selected bootstrap profile. Prepare the demo's native helper,
then install a packed `telemetry-sdk` in a separate browser directory. It is a private
development package; the name alone does not imply a public npm registry release.

```sh
npm install --prefix /external/browser-demo --offline --ignore-scripts /path/to/telemetry-sdk-0.1.0.tgz
cp examples/connection_session/Dynamic.mjs examples/connection_session/index.html /external/browser-demo/
```

Start the reference WebSocket server with `--public-root /external/browser-demo`
and port 8080, then open `http://127.0.0.1:8080`. It checks WebSocket Origin, so
use the browser page from its HTTP origin. A Node host would need an explicit
compatible WebSocket constructor that supplies the server's expected Origin.
The supplied [index.html](index.html) exposes the installed SDK through an import map:

```html
<!doctype html>
<meta charset="utf-8">
<script type="importmap">
{"imports":{"telemetry-sdk":"./node_modules/telemetry-sdk/web/index.js"}}
</script>
<pre id="result">Connecting...</pre>
<script type="module" src="./Dynamic.mjs"></script>
```

Serve these files from the reference server's `--public-root`. Opening the page
reads the first readable Field from the real Descriptor, prints the result with
exact BigInt text and closes the Session. It does not invoke writes or Commands.

[Connection and recovery contract](../../doc/ConnectionSession.md) ·
[Installed Static/Dynamic consumers and native tests](../../tests/connection_session/README.md)
