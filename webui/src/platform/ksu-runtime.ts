export type CommandResult = { stdout: string; stderr: string; code: number };

declare global {
  interface Window {
    __ztCallbacks?: Record<string, (code: number, stdout: string, stderr: string) => void>;
    ksu?: {
      exec?: (command: string, options?: string, callback?: string) => void;
      spawn?: (command: string, args: string, options?: string, callback?: string) => void;
    };
  }
}

type SpawnStream = {
  on: (event: "data", callback: (data: string) => void) => void;
  emit: (event: "data", data: string) => void;
};
type SpawnChild = {
  stdout: SpawnStream;
  stderr: SpawnStream;
  on: (event: "exit" | "error", callback: (value: number | Error) => void) => void;
  emit: (event: "exit" | "error", value: number | Error) => void;
};

let sequence = 0;
const quote = (value: string) => `'${value.replaceAll("'", "'\\''")}'`;

function createStream(): SpawnStream {
  const listeners: Array<(data: string) => void> = [];
  return {
    on(event, callback) { if (event === "data") listeners.push(callback); },
    emit(event, data) { if (event === "data") listeners.forEach((callback) => callback(data)); }
  };
}

function createChild(): SpawnChild {
  const listeners: Record<"exit" | "error", Array<(value: number | Error) => void>> = { exit: [], error: [] };
  return {
    stdout: createStream(),
    stderr: createStream(),
    on(event, callback) { listeners[event].push(callback); },
    emit(event, value) { listeners[event].forEach((callback) => callback(value)); }
  };
}

function rootSpawn(command: string): Promise<CommandResult> {
  return new Promise((resolve, reject) => {
    const ksu = window.ksu;
    if (!ksu?.spawn) { reject(new Error("APatch/KernelSU spawn unavailable")); return; }
    const callbackRef = `__ztSpawn_${Date.now().toString(36)}_${(++sequence).toString(36)}`;
    const child = createChild();
    const callbacks = window as unknown as Record<string, unknown>;
    const stdout: string[] = [];
    const stderr: string[] = [];
    let settled = false;
    const cleanup = () => { delete callbacks[callbackRef]; window.clearTimeout(timeout); };
    const fail = (error: unknown) => { if (settled) return; settled = true; cleanup(); reject(error instanceof Error ? error : new Error(String(error))); };
    child.stdout.on("data", (data) => stdout.push(String(data ?? "")));
    child.stderr.on("data", (data) => stderr.push(String(data ?? "")));
    child.on("exit", (code) => {
      if (settled) return;
      settled = true;
      cleanup();
      resolve({ code: Number(code), stdout: stdout.join("\n"), stderr: stderr.join("\n") });
    });
    child.on("error", fail);
    callbacks[callbackRef] = child;
    const timeout = window.setTimeout(() => fail(new Error("root command timeout")), 120000);
    try { ksu.spawn(command, "[]", "{}", callbackRef); } catch (error) { fail(error); }
  });
}

function rootExec(command: string): Promise<CommandResult> {
  return new Promise((resolve, reject) => {
    const ksu = window.ksu;
    if (!ksu?.exec) { reject(new Error("APatch/KernelSU root runtime unavailable")); return; }
    const callbacks: Record<string, (code: number, stdout: string, stderr: string) => void> =
      window.__ztCallbacks ?? (window.__ztCallbacks = Object.create(null));
    const id = `cb_${Date.now().toString(36)}_${(++sequence).toString(36)}`;
    const timeout = window.setTimeout(() => { delete callbacks[id]; reject(new Error("root command timeout")); }, 120000);
    callbacks[id] = (code, stdout, stderr) => {
      window.clearTimeout(timeout);
      delete callbacks[id];
      resolve({ code: Number(code), stdout: String(stdout || ""), stderr: String(stderr || "") });
    };
    try { ksu.exec(command, "{}", `window.__ztCallbacks.${id}`); }
    catch (error) { window.clearTimeout(timeout); delete callbacks[id]; reject(error); }
  });
}

export async function runRoot(args: string[]): Promise<string> {
  const command = args.map(quote).join(" ");
  const result = window.ksu?.spawn ? await rootSpawn(command) : await rootExec(command);
  const output = result.stdout.trim();
  if (result.code !== 0) throw new Error(result.stderr.trim() || output || `root command failed (${result.code})`);
  return output;
}
