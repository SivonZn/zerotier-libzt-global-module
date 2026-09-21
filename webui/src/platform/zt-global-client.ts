import { runRoot } from "./ksu-runtime";
import { Config, PlanetInfo, Status } from "../domain/models";
const ctl = "/data/adb/modules/zerotier-libzt-global/zt-globalctl";
const parse = <T>(s: string): T => JSON.parse(s) as T;

function bytesToBase64(bytes: Uint8Array): string {
  const parts: string[] = [];
  const step = 0x8000;
  for (let offset = 0; offset < bytes.length; offset += step) {
    parts.push(String.fromCharCode(...bytes.subarray(offset, offset + step)));
  }
  return btoa(parts.join(""));
}

export const client = {
  status: async () => parse<Status>(await runRoot([ctl, "status"])),
  config: async () => parse<Config>(await runRoot([ctl, "config"])),
  save: async (c: Config) => parse<{ok: boolean}>(await runRoot([ctl, "save", String(c.enabled), c.networkId, c.rulePriority, String(c.routingTable), String(c.mtu), String(c.port)])),
  restart: async () => parse<{ok: boolean}>(await runRoot([ctl, "restart"])),
  routes: async () => runRoot([ctl, "routes"]),
  logs: async () => runRoot([ctl, "logs"]),
  planetInfo: async () => parse<PlanetInfo>(await runRoot([ctl, "planet-info"])),
  resetPlanet: async () => parse<{ok: boolean; mode: "official"; planetLoaded: true; restarted: true}>(await runRoot([ctl, "planet-reset"])),
  installPlanet: async (file: File, onProgress?: (percent: number) => void) => {
    if (file.size < 178 || file.size > 8480) throw new Error("Planet 文件大小必须为 178 B–8.3 KiB");
    const tokenBytes = crypto.getRandomValues(new Uint8Array(12));
    const token = Array.from(tokenBytes, (value) => value.toString(16).padStart(2, "0")).join("");
    const encoded = bytesToBase64(new Uint8Array(await file.arrayBuffer()));
    const chunkSize = 32768;
    await runRoot([ctl, "planet-begin", token, String(file.size)]);
    try {
      for (let offset = 0; offset < encoded.length; offset += chunkSize) {
        await runRoot([ctl, "planet-chunk", token, encoded.slice(offset, offset + chunkSize)]);
        onProgress?.(Math.min(99, Math.round(((offset + chunkSize) / encoded.length) * 100)));
      }
      const result = parse<{ok: boolean; mode: "custom"; planetLoaded: true; restarted: true}>(await runRoot([ctl, "planet-commit", token]));
      onProgress?.(100);
      return result;
    } catch (error) {
      try { await runRoot([ctl, "planet-abort", token]); } catch { /* best effort */ }
      throw error;
    }
  }
};
