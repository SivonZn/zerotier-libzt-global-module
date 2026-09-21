import { render } from "solid-js/web";
import { App } from "./app/App";
import "./styles.css";
import "./system-theme.css";
render(() => <App />, document.getElementById("app")!);
