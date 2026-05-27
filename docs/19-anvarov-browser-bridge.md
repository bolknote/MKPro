# Emulator Browser Bridge

The browser bundle lets the Serge Anvarov MK-61 emulator accept M61 source in
its program field. The emulator still receives ordinary MK-61 hex opcodes: the
bridge compiles the field before the page's own "write program to memory"
handler runs.

## Build And Serve

```sh
npm run build:browser
npm run serve:browser
```

This serves the bundle at:

```text
http://127.0.0.1:4173/dist/m61-emulator-bridge.user.js
```

## Console Bootstrap

Open the emulator page, then paste this into the browser console:

```js
(() => {
  const defaultUrl = "http://127.0.0.1:4173/dist/m61-emulator-bridge.user.js";
  const url = window.M61_EMULATOR_URL || defaultUrl;
  const oldScript = document.querySelector("script[data-m61-emulator-loader]");
  oldScript?.remove();

  const script = document.createElement("script");
  script.dataset.m61EmulatorLoader = "true";
  script.src = `${url}${url.includes("?") ? "&" : "?"}t=${Date.now()}`;
  script.onload = () => console.info("[M61] loaded", url);
  script.onerror = () => console.error("[M61] cannot load", url);
  document.head.append(script);
})();
```

The same snippet lives in `tools/emulator-console-loader.js` for copying.

## Use

1. Turn the calculator on.
2. Paste M61 source into the program field.
3. Click the emulator's write-to-memory button.

The bridge detects M61 source, compiles it to hex tokens, replaces the field,
and lets the original emulator click handler continue. If compilation fails,
the click is stopped and the status line under the buttons shows the error.

Useful console helpers:

```js
M61.compile(source).programText
M61Emulator.compileField()
M61Emulator.writeFieldToMemory()
M61Emulator.restoreSource()
M61Emulator.getLastResult()?.listing
```

To load from a different host, set `window.M61_EMULATOR_URL` before running
the loader snippet.
