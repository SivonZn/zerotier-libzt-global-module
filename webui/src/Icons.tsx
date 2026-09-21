import type { JSX } from "solid-js";

export type IconProps = { class?: string };

function Icon(props: IconProps & { children: JSX.Element }) {
  return <svg
    class={props.class}
    viewBox="0 0 24 24"
    fill="none"
    stroke="currentColor"
    stroke-width="1.8"
    stroke-linecap="round"
    stroke-linejoin="round"
    aria-hidden="true"
  >{props.children}</svg>;
}

export function RefreshIcon(props: IconProps) {
  return <Icon {...props}><path d="M20 6v5h-5M4 18v-5h5"/><path d="M18.5 9A7 7 0 0 0 6 6.5L4 9m16 6-2 2.5A7 7 0 0 1 5.5 15"/></Icon>;
}

export function LogIcon(props: IconProps) {
  return <Icon {...props}><path d="M6 3h9l3 3v15H6z"/><path d="M14 3v4h4M9 12h6m-6 4h6"/></Icon>;
}

export function GlobeIcon(props: IconProps) {
  return <Icon {...props}><circle cx="12" cy="12" r="9"/><path d="M3 12h18M12 3a15 15 0 0 1 0 18M12 3a15 15 0 0 0 0 18"/></Icon>;
}

export function NetworkIcon(props: IconProps) {
  return <Icon {...props}><circle cx="12" cy="5" r="2.5"/><circle cx="5" cy="18" r="2.5"/><circle cx="19" cy="18" r="2.5"/><path d="m10.8 7.2-4.6 8.6m7-8.6 4.6 8.6M7.5 18h9"/></Icon>;
}

export function RouteIcon(props: IconProps) {
  return <Icon {...props}><circle cx="6" cy="18" r="2.5"/><path d="M8.5 18h2a3 3 0 0 0 3-3V9a3 3 0 0 1 3-3H20"/><path d="m17 3 3 3-3 3"/></Icon>;
}

export function SettingsIcon(props: IconProps) {
  return <Icon {...props}><path d="M12.22 2h-.44a2 2 0 0 0-2 2v.18a2 2 0 0 1-1 1.73l-.43.25a2 2 0 0 1-2 0l-.15-.08a2 2 0 0 0-2.73.73l-.22.38a2 2 0 0 0 .73 2.73l.15.09a2 2 0 0 1 1 1.74v.5a2 2 0 0 1-1 1.74l-.15.09a2 2 0 0 0-.73 2.73l.22.38a2 2 0 0 0 2.73.73l.15-.08a2 2 0 0 1 2 0l.43.25a2 2 0 0 1 1 1.73V20a2 2 0 0 0 2 2h.44a2 2 0 0 0 2-2v-.18a2 2 0 0 1 1-1.73l.43-.25a2 2 0 0 1 2 0l.15.08a2 2 0 0 0 2.73-.73l.22-.38a2 2 0 0 0-.73-2.73l-.15-.09a2 2 0 0 1-1-1.74v-.5a2 2 0 0 1 1-1.74l.15-.09a2 2 0 0 0 .73-2.73l-.22-.38a2 2 0 0 0-2.73-.73l-.15.08a2 2 0 0 1-2 0l-.43-.25a2 2 0 0 1-1-1.73V4a2 2 0 0 0-2-2Z"/><circle cx="12" cy="12" r="3"/></Icon>;
}

export function FileIcon(props: IconProps) {
  return <Icon {...props}><path d="M6 3h9l3 3v15H6z"/><path d="M14 3v4h4"/></Icon>;
}

export function CheckIcon(props: IconProps) {
  return <Icon {...props}><path d="m5 12 4 4L19 6"/></Icon>;
}

export function WarningIcon(props: IconProps) {
  return <Icon {...props}><path d="M10.3 3.7 2.6 17a2 2 0 0 0 1.7 3h15.4a2 2 0 0 0 1.7-3L13.7 3.7a2 2 0 0 0-3.4 0Z"/><path d="M12 9v4m0 3h.01"/></Icon>;
}
