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
