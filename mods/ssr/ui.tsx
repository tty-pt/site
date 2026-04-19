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
      { href: `/${module}/${id}/edit`, icon: "✏️", label: "edit" },
      { href: `/${module}/${id}/delete`, icon: "🗑️", label: "delete" },
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
          <a className="btn" href={`/auth/login?ret=${path}`}>
            <span>🔑</span>
            <label>login</label>
          </a>
          <a className="btn" href="/auth/register">
            <span>📝</span>
            <label>register</label>
          </a>
        </>
      )}
    </>
  );
}

export function Layout({ children, user, title, path, menuItems, icon }: {
  children: any;
  user: string | null;
  title: string;
  path: string;
  menuItems?: any;
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
