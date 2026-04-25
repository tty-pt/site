import type { ComponentChildren } from "preact";

/**
 * parseErrorBody — if body is a urlencoded `error=...&status=...` envelope
 * (as emitted by call_respond_error in C), returns { error, status } so the
 * caller can short-circuit into ctx.render with the matching HTTP status.
 * Returns null when body is not an error envelope.
 */
export function parseErrorBody(text: string): { error: string; status: number } | null {
  const params = new URLSearchParams(text);
  const error = params.get("error");
  if (!error) return null;
  return { error, status: Number(params.get("status") ?? "500") };
}

export type PostedResult<T> =
  | { ok: true; data: T }
  | { ok: false; error: string; status: number };

export async function readPostedJson<T>(req: Request): Promise<PostedResult<T>> {
  const text = await req.text();
  const err = parseErrorBody(text);
  if (err) return { ok: false, ...err };
  return { ok: true, data: JSON.parse(text) as T };
}

export async function readPostedForm(req: Request): Promise<URLSearchParams> {
  return new URLSearchParams(await req.text());
}

export function modulePath(module: string): string {
  return `/${module}`;
}

export function moduleCollectionPath(module: string): string {
  return `/${module}/`;
}

export function moduleItemPath(module: string, id: string): string {
  return `/${module}/${id}`;
}

export function moduleItemActionPath(module: string, id: string, action: string): string {
  return `/${module}/${id}/${action}`;
}

export function moduleActionPath(module: string, action: string): string {
  return `/${module}/${action}`;
}

export function authPath(action: string): string {
  return `/auth/${action}`;
}

export function loginRedirect(req: Request): Response {
  const reqUrl = new URL(req.url);
  const forwardedHost = req.headers.get("X-Forwarded-Host");
  if (forwardedHost) {
    reqUrl.host = forwardedHost;
  }
  return Response.redirect(new URL(authLoginHref(reqUrl.pathname + reqUrl.search), reqUrl), 303);
}

export function authLoginHref(ret: string): string {
  return `${authPath("login")}?${new URLSearchParams({ ret }).toString()}`;
}

export interface OwnerAction {
  href: string;
  icon: string;
  label: string;
}

export function OwnerActions({ actions }: { actions: OwnerAction[] }) {
  return (
    <>
      {actions.map(({ href, icon, label }) => (
        <a key={href} href={href} className="btn">
          <span>{icon}</span><label>{label}</label>
        </a>
      ))}
    </>
  );
}

export function ItemMenu({ module, id, isOwner }: { module: string; id: string; isOwner: boolean }) {
  if (!isOwner) return null;
  return (
    <OwnerActions actions={[
      { href: moduleItemActionPath(module, id, "edit"), icon: "✏️", label: "edit" },
      { href: moduleItemActionPath(module, id, "delete"), icon: "🗑️", label: "delete" },
    ]} />
  );
}

function parentPath(path: string): string {
  const parts = path.replace(/\/$/, "").split("/").filter(Boolean);
  parts.pop();
  return "/" + parts.join("/");
}

export function Menu({ user, path, icon }: { user: string | null; path: string; icon?: string }) {
  const isHome = path === "/" || path === "";
  return (
    <>
      {!isHome && (
        <a className="btn" href={parentPath(path)}>
          <span>{icon ?? "🏠"}</span>
          <label>go up</label>
        </a>
      )}
      {user ? (
        <>
          <a className="btn" href={`/${user}/`}>
            <span>😊</span>
            <label>me</label>
          </a>
          <a className="btn" href="/auth/logout">
            <span>🚪</span>
            <label>logout</label>
          </a>
        </>
      ) : (
        <>
          <a className="btn" href={authLoginHref(path)}>
            <span>🔑</span>
            <label>login</label>
          </a>
          <a className="btn" href={authPath("register")}>
            <span>📝</span>
            <label>register</label>
          </a>
        </>
      )}
    </>
  );
}

export function ErrorPage({ status, message, user, path }: {
  status: number;
  message: string;
  user: string | null;
  path: string;
}) {
  return (
    <Layout user={user} title={`${status}`} path={path}>
      <p>{message}</p>
    </Layout>
  );
}

export function PageSection({ title, children }: {
  title: string;
  children: ComponentChildren;
}) {
  return (
    <>
      <h3>{title}</h3>
      {children}
    </>
  );
}

export function EmptyState({ message }: { message: string }) {
  return <p className="text-muted">{message}</p>;
}

export function Field({ label, children }: {
  label: string;
  children: ComponentChildren;
}) {
  return (
    <label>
      {label}
      {children}
    </label>
  );
}

export function FormActions({
  cancelHref,
  submitLabel = "Save Changes",
  cancelLabel = "Cancel",
  children,
}: {
  cancelHref: string;
  submitLabel?: string;
  cancelLabel?: string;
  children?: ComponentChildren;
}) {
  return (
    <div className="flex gap-2">
      <button type="submit" className="btn btn-primary">
        {submitLabel}
      </button>
      {children}
      <a href={cancelHref} className="btn btn-secondary">
        {cancelLabel}
      </a>
    </div>
  );
}

export function FormPage({
  user,
  title,
  path,
  icon,
  heading,
  children,
}: {
  user: string | null;
  title: string;
  path: string;
  icon?: string;
  heading?: string;
  children: ComponentChildren;
}) {
  return (
    <Layout user={user} title={title} path={path} icon={icon}>
      <div className="center">
        {heading && <h1>{heading}</h1>}
        {children}
      </div>
    </Layout>
  );
}

export function Layout({ children, user, title, path, menuItems, icon }: {
  children: ComponentChildren;
  user: string | null;
  title: string;
  path: string;
  menuItems?: ComponentChildren;
  icon?: string;
}) {
  return (<div>
    <label className="menu">
      <input
        name="functions"
        type="checkbox"
        class="hidden"
      ></input>

      <span className="fixed top-0 right-0 z-10 p-2 flex items-center gap-4">
        <a className="menu-toggle flex items-center justify-center cursor-pointer text-base btn">⚙️</a>
      </span>

      <span className="functions flex-1 fixed right-0 z-20 h-full overflow-y-auto text-sm capitalize flex flex-col gap-2 p-4">
        <Menu user={user} path={path} icon={icon} />
        {menuItems && (
          <>
            <div className="menu-separator"></div>
            <div className="module-menu">
              {menuItems}
            </div>
          </>
        )}
      </span>
    </label>

    <div className="main">
      <h1>{title}</h1>
      {children}
    </div>
    <script src="/app.js" defer></script>
  </div>);
}
