import { createEffect, createSignal, For, onCleanup, onMount, Show } from "solid-js";
import { createConfigModel } from "../application/createConfigModel";
import { OverviewPage } from "../features/overview/OverviewPage";
import { NetworkPage } from "../features/network/NetworkPage";
import { RoutingPage } from "../features/routing/RoutingPage";
import { SettingsPage } from "../features/settings/SettingsPage";
import { client } from "../platform/zt-global-client";
import { Config, PlanetInfo } from "../domain/models";
import { createPageNavigation, type PageId } from "./createPageNavigation";
import { ConfirmDialog } from "../ConfirmDialog";
import type { Component } from "solid-js";
import { GlobeIcon, LogIcon, NetworkIcon, RefreshIcon, RouteIcon, SettingsIcon, type IconProps } from "../Icons";

export function App() {
  const model = createConfigModel();
  const navigation = createPageNavigation();
  const [draft, setDraft] = createSignal<Config>(model.config());
  const [toast, setToast] = createSignal("");
  const [logs, setLogs] = createSignal<string | undefined>();
  const [applying, setApplying] = createSignal(false);
  const [planetBusy, setPlanetBusy] = createSignal(false);
  const [planetProgress, setPlanetProgress] = createSignal(0);
  const [planetInfo, setPlanetInfo] = createSignal<PlanetInfo>();
  const [planetResetConfirmOpen, setPlanetResetConfirmOpen] = createSignal(false);
  const logHistoryKey = `zt-log-${Math.random().toString(36).slice(2)}`;
  const confirmHistoryKey = `zt-confirm-${Math.random().toString(36).slice(2)}`;
  let ownsLogHistoryEntry = false;
  let ownsConfirmHistoryEntry = false;
  const loadPlanetInfo = async () => {
    try { setPlanetInfo(await client.planetInfo()); } catch { setPlanetInfo(undefined); }
  };
  const refreshAll = async () => { await Promise.all([model.load(), loadPlanetInfo()]); };
  const openLogs = async () => {
    const content = await client.logs();
    if (logs() !== undefined) return;
    window.history.pushState({ ...window.history.state, ztLogOverlay: logHistoryKey }, "");
    ownsLogHistoryEntry = true;
    setLogs(content || "暂无运行日志");
  };
  const closeLogs = (fromHistory = false) => {
    if (logs() === undefined) return;
    const shouldGoBack = !fromHistory && ownsLogHistoryEntry && window.history.state?.ztLogOverlay === logHistoryKey;
    ownsLogHistoryEntry = false;
    setLogs(undefined);
    if (shouldGoBack) window.history.back();
  };
  const openPlanetResetConfirm = () => {
    if (applying() || planetBusy() || planetResetConfirmOpen()) return;
    window.history.pushState({ ...window.history.state, ztConfirmOverlay: confirmHistoryKey }, "");
    ownsConfirmHistoryEntry = true;
    setPlanetResetConfirmOpen(true);
  };
  const closePlanetResetConfirm = (fromHistory = false) => {
    if (!planetResetConfirmOpen()) return;
    const shouldGoBack = !fromHistory && ownsConfirmHistoryEntry && window.history.state?.ztConfirmOverlay === confirmHistoryKey;
    ownsConfirmHistoryEntry = false;
    setPlanetResetConfirmOpen(false);
    if (shouldGoBack) window.history.back();
  };
  onMount(() => {
    let statusTimer: number | undefined;
    let disposed = false;
    const handlePopState = () => {
      if (planetResetConfirmOpen()) closePlanetResetConfirm(true);
      else if (logs() !== undefined) closeLogs(true);
    };
    const scheduleStatusRefresh = (delay = 500) => {
      if (statusTimer !== undefined) window.clearTimeout(statusTimer);
      statusTimer = window.setTimeout(async () => {
        statusTimer = undefined;
        if (disposed || document.visibilityState !== "visible") return;
        try { await model.loadStatus(); } catch { /* retain the latest known state on a transient bridge error */ }
        if (!disposed && document.visibilityState === "visible") scheduleStatusRefresh();
      }, delay);
    };
    const handleVisibilityChange = () => {
      if (document.visibilityState === "visible") scheduleStatusRefresh(0);
      else if (statusTimer !== undefined) {
        window.clearTimeout(statusTimer);
        statusTimer = undefined;
      }
    };
    window.addEventListener("popstate", handlePopState);
    document.addEventListener("visibilitychange", handleVisibilityChange);
    scheduleStatusRefresh();
    onCleanup(() => {
      disposed = true;
      if (statusTimer !== undefined) window.clearTimeout(statusTimer);
      window.removeEventListener("popstate", handlePopState);
      document.removeEventListener("visibilitychange", handleVisibilityChange);
    });
  });
  createEffect(() => setDraft({...model.config(), enabled: true}));
  createEffect(() => { void refreshAll(); });
  const nav: Array<{ id: PageId; label: string; icon: Component<IconProps> }> = [
    { id: "overview", label: "概览", icon: GlobeIcon },
    { id: "network", label: "网络", icon: NetworkIcon },
    { id: "routing", label: "路由", icon: RouteIcon },
    { id: "settings", label: "设置", icon: SettingsIcon }
  ];
  const save = async () => {
    if (applying() || planetBusy()) return;
    setApplying(true);
    try {
      const ok = await model.save({...draft(), enabled: true});
      setToast(ok ? "配置已应用并重启" : "应用失败：" + model.message());
      setTimeout(() => setToast(""), 2800);
    } finally { setApplying(false); }
  };
  const installPlanet = async (file: File) => {
    if (applying() || planetBusy()) return;
    setPlanetBusy(true);
    setPlanetProgress(0);
    try {
      await client.installPlanet(file, setPlanetProgress);
      await model.load();
      await loadPlanetInfo();
      setToast(`Planet 已加载：${file.name}`);
    } catch (error) {
      setToast(`Planet 安装失败：${error instanceof Error ? error.message : String(error)}`);
    } finally {
      await loadPlanetInfo();
      setPlanetBusy(false);
      setTimeout(() => setToast(""), 3600);
    }
  };
  const resetPlanet = async () => {
    if (applying() || planetBusy()) return;
    setPlanetBusy(true);
    setPlanetProgress(0);
    try {
      await client.resetPlanet();
      await model.load();
      await loadPlanetInfo();
      setToast("ZeroTier 官方 Planet 已加载");
    } catch (error) {
      setToast(`Planet 重置失败：${error instanceof Error ? error.message : String(error)}`);
    } finally {
      await loadPlanetInfo();
      setPlanetBusy(false);
      setTimeout(() => setToast(""), 3600);
    }
  };
  const acceptPlanetReset = () => {
    closePlanetResetConfirm();
    void resetPlanet();
  };
  return <div class="app-shell">
    <header class="app-header">
      <div class="brand"><div class="brand-mark">✦</div><div class="brand-copy"><strong>ZeroTier libzt</strong><small>APatch / KernelSU WebUI</small></div></div>
      <button class="icon-button header-log-button" data-no-page-drag onClick={() => void openLogs()} aria-label="查看运行日志" title="运行日志">
        <LogIcon />
      </button>
      <button class="icon-button" data-no-page-drag onClick={() => void refreshAll()} aria-label="刷新" title="刷新"><RefreshIcon /></button>
      <button class="header-apply-button" data-no-page-drag disabled={applying() || planetBusy()} onClick={save}>{applying() ? "应用中..." : "应用"}</button>
    </header>
    <div class="page-viewport" ref={navigation.setViewport} onTouchStart={navigation.handleInputTouchStart} onTouchEnd={navigation.handleInputTouchEnd} onTouchCancel={navigation.handleInputTouchCancel}>
      <div class="page-track">
        <OverviewPage status={model.status()} config={model.config()} />
        <NetworkPage draft={draft()} setDraft={setDraft} planetInfo={planetInfo()} planetBusy={planetBusy() || applying()} planetProgress={planetProgress()} onPlanetSelected={installPlanet} onPlanetReset={openPlanetResetConfirm} />
        <RoutingPage draft={draft()} setDraft={setDraft} />
        <SettingsPage />
      </div>
    </div>
    <nav class="page-navigation" classList={{ dragging: navigation.pageDragging() }} style={{ "--page-progress": navigation.pageProgress() }} aria-label="页面导航">
      <span class="page-nav-indicator" aria-hidden="true" />
      <For each={nav}>{(item, index) => {
        const PageIcon = item.icon;
        return <button class="page-nav-item" classList={{ active: navigation.activePage() === item.id }} aria-current={navigation.activeIndex() === index() ? "page" : undefined} onClick={() => navigation.activatePage(item.id)}><PageIcon />{item.label}</button>;
      }}</For>
    </nav>
    <Show when={toast()}><div id="toast-container"><div class="toast success">{toast()}</div></div></Show>
    <Show when={logs() !== undefined}><div class="dialog-layer"><button class="dialog-backdrop" onClick={() => closeLogs()} /><div class="log-dialog"><header><h2>运行日志</h2><button class="dialog-close" onClick={() => closeLogs()}>×</button></header><pre>{logs()}</pre></div></div></Show>
    <ConfirmDialog open={planetResetConfirmOpen()} title="恢复官方 Planet" message="此操作会移除当前自定义 Planet 与兼容 Roots，并重启 ZeroTier 节点。使用自建 Planet 的网络可能暂时无法连接。" confirmLabel="确认恢复" onDismiss={() => closePlanetResetConfirm()} onAccept={acceptPlanetReset} />
  </div>;
}
