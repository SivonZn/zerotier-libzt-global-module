import type { Component } from "solid-js";
import { GlobeIcon, SettingsIcon } from "../../Icons";

export const SettingsPage: Component = () =>
  <main class="page-panel"><div class="page-content">
    <div class="page-intro"><div class="intro-icon"><SettingsIcon /></div><div><h1>设置</h1></div></div>
    <section class="card section-card module-about-card">
      <div class="module-about-brand">
        <span class="module-about-mark"><GlobeIcon /></span>
        <div><h2>ZeroTier libzt Global</h2><p>面向 Android Root 环境的轻量级 ZeroTier 全局网络模块</p></div>
      </div>
    </section>
  </div></main>;
