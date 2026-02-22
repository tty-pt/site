import React from "https://esm.sh/react@18";
import Login from "./components/Login.tsx";
import Register from "./components/Register.tsx";
import ConfirmUI from "./components/ConfirmUI.tsx";

export const routes = ["/login", "/register", "/confirm-page"];

export async function render({ user, path, params }: { user: string | null; path: string; params: Record<string,string> }) {
  if (path === "/login") {
    return Login({ user, path });
  }

  if (path === "/register") {
    return Register({ user, path });
  }

  if (path === "/confirm-page") {
    return ConfirmUI({ user, path, params });
  }

  return null;
}
