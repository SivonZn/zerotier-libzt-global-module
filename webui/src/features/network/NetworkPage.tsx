import { createSignal, Show, type Component } from "solid-js";
import type { Config, PlanetInfo } from "../../domain/models";
import { FileIcon, NetworkIcon } from "../../Icons";

type NetworkPageProps = {
  draft: Config;
  setDraft: (config: Config) => void;
  planetInfo?: PlanetInfo;
  planetBusy: boolean;
  planetProgress: number;
  onPlanetSelected: (file: File) => Promise<void>;
  onPlanetReset: () => void;
};

export const NetworkPage: Component<NetworkPageProps> = (props) => {
  const [selectedName, setSelectedName] = createSignal("");
  const selectPlanet = async (event: Event) => {
    const input = event.currentTarget as HTMLInputElement;
    const file = input.files?.[0];
    input.value = "";
    if (!file) return;
    setSelectedName(file.name);
    await props.onPlanetSelected(file);
  };

  return <main class="page-panel" data-page="network"><div class="page-content">
    <div class="page-intro"><div class="intro-icon"><NetworkIcon /></div><div><h1>网络</h1></div></div>
    <section class="card section-card">
      <h2>连接参数</h2>
      <label class="field-label">Network ID<input class="text-input page-swipe-input" data-no-page-drag value={props.draft.networkId} maxlength="16" onInput={(event) => props.setDraft({...props.draft, networkId: event.currentTarget.value})} placeholder="16 位十六进制" /></label>
      <label class="field-label">MTU<input class="text-input page-swipe-input" data-no-page-drag type="number" min="1280" max="2800" value={props.draft.mtu} onInput={(event) => props.setDraft({...props.draft, mtu: Number(event.currentTarget.value)})} /></label>
    </section>

    <section class="card section-card planet-card">
      <div class="section-heading planet-heading"><h2>自定义 Planet</h2><span class="planet-mode-badge" classList={{ custom: props.planetInfo?.mode === "custom", official: props.planetInfo?.mode === "official" }}>{props.planetInfo ? props.planetInfo.mode === "custom" ? "当前：自定义 Planet" : "当前：官方 Planet" : "当前：读取中"}</span></div>
      <input id="planet-file" class="file-input" type="file" disabled={props.planetBusy} onChange={selectPlanet} />
      <label for="planet-file" classList={{ "planet-picker": true, disabled: props.planetBusy }}>
        <span class="planet-picker-icon"><FileIcon /></span>
        <span class="planet-picker-copy"><strong>{props.planetBusy ? props.planetProgress >= 99 ? "加载中..." : `复制中... ${props.planetProgress}%` : "选择 Planet 文件"}</strong><Show when={selectedName()}><small>{selectedName()}</small></Show></span>
        <span class="planet-picker-arrow">›</span>
      </label>
      <Show when={props.planetBusy}><progress class="planet-progress" max="100" value={props.planetProgress} /></Show>
      <div class="inline-actions planet-actions"><button type="button" class="primary-button" data-no-page-drag disabled={props.planetBusy} onClick={props.onPlanetReset}>恢复官方 Planet</button></div>
    </section>
  </div></main>;
};
