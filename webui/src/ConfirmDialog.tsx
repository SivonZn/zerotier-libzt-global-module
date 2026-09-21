import { Show, type Component } from "solid-js";
import { WarningIcon } from "./Icons";

export const ConfirmDialog: Component<{
  open: boolean;
  title: string;
  message: string;
  confirmLabel: string;
  onDismiss: () => void;
  onAccept: () => void;
}> = (props) => <Show when={props.open}>
  <div class="dialog-layer confirm-layer" data-no-page-drag role="presentation">
    <button class="dialog-backdrop" aria-label="取消确认" onClick={props.onDismiss} />
    <section class="confirm-dialog" role="alertdialog" aria-modal="true" aria-labelledby="confirm-title" aria-describedby="confirm-message">
      <div class="confirm-mark" aria-hidden="true"><WarningIcon /></div>
      <div><h2 id="confirm-title">{props.title}</h2><p id="confirm-message">{props.message}</p></div>
      <div class="confirm-actions">
        <button type="button" class="secondary-button" onClick={props.onDismiss}>取消</button>
        <button type="button" class="danger-button" onClick={props.onAccept}>{props.confirmLabel}</button>
      </div>
    </section>
  </div>
</Show>;
