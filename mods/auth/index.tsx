import Login from "./Login.tsx";
import Register from "./Register.tsx";
import ConfirmUI from "./ConfirmUI.tsx";

export const routes = ["/login", "/register", "/confirm"];

export function render({ user, path, params }: {
  user: string | null;
  path: string;
  params: Record<string,string>
}) {
  if (path === "/login")
    return Login({ user, path });

  if (path === "/register")
    return Register({ user, path });

  if (path === "/confirm")
    return ConfirmUI({ user, path, params });

  return null;
}
