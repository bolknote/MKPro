# Emulator Browser Bridge

The browser bundle lets the Serge Anvarov MK-61 emulator accept MK-Pro source in
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
http://127.0.0.1:4173/dist/mk-pro-emulator-bridge.user.js
```

## Console Bootstrap

Open the emulator page, then paste this into the browser console:

```js
(() => {
  const defaultUrl = "http://127.0.0.1:4173/dist/mk-pro-emulator-bridge.user.js";
  const url = window.MK_PRO_EMULATOR_URL || defaultUrl;
  const oldScript = document.querySelector("script[data-mk-pro-emulator-loader]");
  oldScript?.remove();

  const script = document.createElement("script");
  script.dataset.mkProEmulatorLoader = "true";
  script.src = `${url}${url.includes("?") ? "&" : "?"}t=${Date.now()}`;
  script.onload = () => console.info("[MK-Pro] loaded", url);
  script.onerror = () => console.error("[MK-Pro] cannot load", url);
  document.head.append(script);
})();
```

The same snippet lives in `tools/emulator-console-loader.js` for copying.

## Use

1. Turn the calculator on.
2. Paste MK-Pro source into the program field.
3. Click the emulator's write-to-memory button.

The bridge detects MK-Pro source, compiles it to hex tokens, replaces the field,
and lets the original emulator click handler continue. If compilation fails,
the click is stopped and the status line under the buttons shows the error.

Useful console helpers:

```js
MKPro.compile(source).programText
MKPro.compile(source).setupBlockText
MKPro.compile(source).setupProgramText
MKProEmulator.compileField()
MKProEmulator.writeFieldToMemory()
MKProEmulator.restoreSource()
MKProEmulator.getLastResult()?.listing
```

When the compiled program needs register initialization, `setupBlockText`
contains an emulator-readable assignment block such as `` `R3=0; Rc=20` ``.
When state initialization needs executable work (for example `random_cell(...)`)
or the optimizer selects an unusual compiler-owned preload such as `1|-00`,
`setupProgramText` contains the setup program that must be run once before
loading the main `programText`.

To load from a different host, set `window.MK_PRO_EMULATOR_URL` before running
the loader snippet.
