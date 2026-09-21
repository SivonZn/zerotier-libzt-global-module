import { createSignal } from "solid-js";
import { client } from "../platform/zt-global-client";
import { Config, Status, defaults } from "../domain/models";
export function createConfigModel() {
  const [config, setConfig] = createSignal<Config>(defaults);
  const [status, setStatus] = createSignal<Status>({ running: false, nodeOnline: false });
  const [busy, setBusy] = createSignal(false);
  const [message, setMessage] = createSignal("");
  let statusRequest: Promise<void> | undefined;
  const loadStatus = () => {
    if (statusRequest) return statusRequest;
    statusRequest = client.status().then((nextStatus) => { setStatus(nextStatus); }).finally(() => { statusRequest = undefined; });
    return statusRequest;
  };
  const load = async () => { setBusy(true); try { const nextConfig = await client.config(); await loadStatus(); setConfig(nextConfig); setMessage(""); } catch (e) { setMessage(e instanceof Error ? e.message : String(e)); } finally { setBusy(false); } };
  const save = async (next: Config) => { setBusy(true); try { await client.save(next); setConfig(next); await client.restart(); await load(); return true; } catch (e) { setMessage(e instanceof Error ? e.message : String(e)); return false; } finally { setBusy(false); } };
  return { config, setConfig, status, busy, message, setMessage, loadStatus, load, save };
}
