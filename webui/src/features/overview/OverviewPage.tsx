import type { Component } from "solid-js";
import type { Config, Status } from "../../domain/models";
import { GlobeIcon } from "../../Icons";

type OverviewPageProps = {
  status: Status;
  config: Config;
};

export const OverviewPage: Component<OverviewPageProps> = (props) => {
  const connected = () => props.status.dataPlaneReady ?? (props.status.nodeOnline && props.status.frameBridge !== false);
  const connectionText = () => connected() ? "已连接" : props.status.running ? "连接中" : "已断开";
  const connectionState = () => connected() ? "connected" : props.status.running ? "connecting" : "disconnected";

  return <main class="page-panel"><div class="page-content overview-page-content">
    <section class="card connection-card" classList={{ connected: connectionState() === "connected", connecting: connectionState() === "connecting", disconnected: connectionState() === "disconnected" }} aria-label={`连接状态：${connectionText()}`}>
      <span class="connection-icon"><GlobeIcon /></span>
      <span class="connection-copy"><small>连接状态</small><strong>{connectionText()}</strong></span>
    </section>
    {props.status.running && !props.status.dataPlaneReady && props.status.lastError &&
      <div class="status-error" role="status">{props.status.lastError}</div>}
    <section class="overview-card-grid" aria-label="网络状态详情">
      <article class="card status-info-card"><span class="label">Network ID</span><strong>{props.config.networkId || "未配置"}</strong></article>
      <article class="card status-info-card"><span class="label">ZeroTier 地址</span><strong>{(props.status.addresses || []).join(", ") || "等待分配"}</strong></article>
      <article class="card status-info-card"><span class="label">接口</span><strong>{props.status.interface || props.config.interface}</strong></article>
    </section>
  </div></main>;
};
