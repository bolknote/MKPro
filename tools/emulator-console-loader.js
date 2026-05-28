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
