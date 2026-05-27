import { CompileError, compileM61, formatListing } from "../core/index.ts";
import type { CompileOptions, CompileResult } from "../core/index.ts";

const DEFAULT_COMPILE_OPTIONS: CompileOptions = {
  opt: "max",
  delivery: "hex",
  budget: 105,
  warnUnsafe: true,
};

const DEFAULT_DEBOUNCE_MS = 250;
const STATUS_ID = "m61-emulator-status";

export interface BrowserCompileOutput {
  source: string;
  programText: string;
  listing: string;
  steps: CompileResult["steps"];
  report: CompileResult["report"];
  diagnostics: CompileResult["diagnostics"];
}

export interface EmulatorBridgeOptions {
  compilerOptions?: Partial<CompileOptions>;
  debounceMs?: number;
  autoWriteOnPaste?: boolean;
}

export interface EmulatorBridge {
  compileField(): BrowserCompileOutput | undefined;
  writeFieldToMemory(): BrowserCompileOutput | undefined;
  restoreSource(): boolean;
  uninstall(): void;
  getLastResult(): BrowserCompileOutput | undefined;
  statusElement: HTMLElement;
}

export interface M61BrowserApi {
  compile(source: string, options?: Partial<CompileOptions>): BrowserCompileOutput;
  compileToProgramText(source: string, options?: Partial<CompileOptions>): string;
  installEmulatorBridge(options?: EmulatorBridgeOptions): EmulatorBridge;
  looksLikeM61Source(text: string): boolean;
}

declare global {
  interface Window {
    M61?: M61BrowserApi;
    M61Emulator?: EmulatorBridge;
    __m61EmulatorBridge?: EmulatorBridge;
  }
}

export function compileForBrowser(
  source: string,
  options: Partial<CompileOptions> = {},
): BrowserCompileOutput {
  const result = compileM61(source, { ...DEFAULT_COMPILE_OPTIONS, ...options });
  const programText = formatProgramTokens(result.steps.map((step) => step.hex));
  return {
    source,
    programText,
    listing: formatListing(result),
    steps: result.steps,
    report: result.report,
    diagnostics: result.diagnostics,
  };
}

export function compileToProgramText(
  source: string,
  options: Partial<CompileOptions> = {},
): string {
  return compileForBrowser(source, options).programText;
}

export function looksLikeM61Source(text: string): boolean {
  const normalized = text.trim();
  return (
    /\btarget\s+mk61\b/iu.test(normalized) ||
    /\bbudget\s+\d+\s+cells\b/iu.test(normalized) ||
    /\boptimize\s+size\b/iu.test(normalized) ||
    /\bprogram\s+[A-Za-z_][\w-]*\s*\{/u.test(normalized)
  );
}

export function installEmulatorBridge(
  options: EmulatorBridgeOptions = {},
): EmulatorBridge {
  window.__m61EmulatorBridge?.uninstall();

  const program = document.getElementById("program");
  const writeButton = document.getElementById("text_to_program");
  if (!(program instanceof HTMLElement)) {
    throw new Error("M61 bridge cannot find #program on this page.");
  }
  if (!(writeButton instanceof HTMLElement)) {
    throw new Error("M61 bridge cannot find #text_to_program on this page.");
  }

  const statusElement = ensureStatusElement(writeButton);
  const cleanups: Array<() => void> = [];
  const compilerOptions = options.compilerOptions ?? {};
  const debounceMs = options.debounceMs ?? DEFAULT_DEBOUNCE_MS;
  const autoWriteOnPaste = options.autoWriteOnPaste ?? false;
  let lastResult: BrowserCompileOutput | undefined;
  let lastSource: string | undefined;
  let previewTimer: number | undefined;

  const setStatus = (message: string, isError = false): void => {
    statusElement.textContent = message;
    statusElement.style.color = isError ? "#ff9b9b" : "#b8ffd8";
  };

  const compileField = (): BrowserCompileOutput | undefined => {
    const source = readProgramText(program);
    if (!looksLikeM61Source(source)) return undefined;

    const compiled = compileForBrowser(source, compilerOptions);
    lastSource = source;
    lastResult = compiled;
    writeProgramText(program, compiled.programText);
    program.dispatchEvent(new CustomEvent("m61:compiled", {
      bubbles: true,
      detail: compiled,
    }));
    setStatus(`M61 compiled: ${compiled.report.steps}/${compiled.report.budget} cells.`);
    return compiled;
  };

  const writeFieldToMemory = (): BrowserCompileOutput | undefined => {
    const compiled = compileField();
    writeButton.click();
    return compiled;
  };

  const restoreSource = (): boolean => {
    if (lastSource === undefined) return false;
    writeProgramText(program, lastSource);
    setStatus("M61 source restored.");
    return true;
  };

  const uninstall = (): void => {
    if (previewTimer !== undefined) window.clearTimeout(previewTimer);
    for (const cleanup of cleanups.splice(0)) cleanup();
    statusElement.remove();
    if (window.__m61EmulatorBridge === bridge) {
      delete window.__m61EmulatorBridge;
    }
    if (window.M61Emulator === bridge) {
      delete window.M61Emulator;
    }
  };

  const bridge: EmulatorBridge = {
    compileField,
    writeFieldToMemory,
    restoreSource,
    uninstall,
    getLastResult: () => lastResult,
    statusElement,
  };

  const beforeEmulatorWrite = (event: Event): void => {
    try {
      compileField();
    } catch (error) {
      event.preventDefault();
      event.stopImmediatePropagation();
      setStatus(errorToStatus(error), true);
      console.error("[M61] compile failed", error);
    }
  };

  const previewCurrentText = (): void => {
    const source = readProgramText(program);
    if (!looksLikeM61Source(source)) return;
    try {
      const compiled = compileForBrowser(source, compilerOptions);
      lastResult = compiled;
      setStatus(`M61 detected: ${compiled.report.steps}/${compiled.report.budget} cells. Click Write to load.`);
    } catch (error) {
      setStatus(errorToStatus(error), true);
    }
  };

  const schedulePreview = (): void => {
    if (previewTimer !== undefined) window.clearTimeout(previewTimer);
    previewTimer = window.setTimeout(previewCurrentText, debounceMs);
  };

  const afterPaste = (): void => {
    window.setTimeout(() => {
      if (autoWriteOnPaste && looksLikeM61Source(readProgramText(program))) {
        writeFieldToMemory();
      } else {
        previewCurrentText();
      }
    }, 0);
  };

  writeButton.addEventListener("click", beforeEmulatorWrite, true);
  program.addEventListener("input", schedulePreview);
  program.addEventListener("paste", afterPaste);
  cleanups.push(() => writeButton.removeEventListener("click", beforeEmulatorWrite, true));
  cleanups.push(() => program.removeEventListener("input", schedulePreview));
  cleanups.push(() => program.removeEventListener("paste", afterPaste));

  setStatus("M61 bridge ready. Paste M61 source, then click Write.");
  window.__m61EmulatorBridge = bridge;
  window.M61Emulator = bridge;
  return bridge;
}

function formatProgramTokens(tokens: string[]): string {
  const rows: string[] = [];
  for (let index = 0; index < tokens.length; index += 16) {
    rows.push(tokens.slice(index, index + 16).join(" "));
  }
  return rows.join("\n");
}

function readProgramText(element: HTMLElement): string {
  const text = element.innerText ?? element.textContent;
  return (text ?? "").replace(/\u00a0/gu, " ").trim();
}

function writeProgramText(element: HTMLElement, text: string): void {
  element.innerHTML = text
    .split(/\r?\n/u)
    .map(escapeHtml)
    .join("<br>");
}

function ensureStatusElement(anchor: HTMLElement): HTMLElement {
  const oldStatus = document.getElementById(STATUS_ID);
  if (oldStatus instanceof HTMLElement) oldStatus.remove();

  const status = document.createElement("div");
  status.id = STATUS_ID;
  status.style.marginTop = "6px";
  status.style.font = "13px monospace";
  status.style.color = "#b8ffd8";
  anchor.parentElement?.append(status);
  return status;
}

function escapeHtml(text: string): string {
  return text
    .replace(/&/gu, "&amp;")
    .replace(/</gu, "&lt;")
    .replace(/>/gu, "&gt;");
}

function errorToStatus(error: unknown): string {
  if (error instanceof CompileError) {
    return error.diagnostics.map((diagnostic) => {
      const line = diagnostic.line === undefined ? "" : `:${diagnostic.line}`;
      return `${diagnostic.level.toUpperCase()}${line}: ${diagnostic.message}`;
    }).join(" | ");
  }
  if (error instanceof Error) return error.message;
  return String(error);
}

const api: M61BrowserApi = {
  compile: compileForBrowser,
  compileToProgramText,
  installEmulatorBridge,
  looksLikeM61Source,
};

if (typeof window !== "undefined") {
  window.M61 = api;
  try {
    installEmulatorBridge();
    console.info("[M61] Emulator bridge installed.");
  } catch (error) {
    console.warn("[M61] Compiler loaded, but the emulator bridge was not installed.", error);
  }
}
