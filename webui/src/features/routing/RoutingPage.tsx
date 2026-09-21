import type { Component } from "solid-js";
import type { Config } from "../../domain/models";
import { RouteIcon } from "../../Icons";

export const RoutingPage: Component<{draft: Config; setDraft: (config: Config) => void}> = (props) => <main class="page-panel"><div class="page-content">
  <div class="page-intro"><div class="intro-icon"><RouteIcon /></div><div><h1>路由</h1></div></div>
  <section class="card section-card">
    <h2>策略</h2>
    <label class="field-label">规则优先级<input class="text-input" value={props.draft.rulePriority} onInput={(event) => props.setDraft({...props.draft, rulePriority: event.currentTarget.value})} placeholder="auto 或 2–32764" /></label>
    <label class="field-label">路由表<input class="text-input" type="number" min="1" value={props.draft.routingTable} onInput={(event) => props.setDraft({...props.draft, routingTable: Number(event.currentTarget.value)})} /></label>
    <label class="field-label">UDP 端口<input class="text-input" type="number" min="0" max="65535" value={props.draft.port} onInput={(event) => props.setDraft({...props.draft, port: Number(event.currentTarget.value)})} /></label>
  </section>
</div></main>;
