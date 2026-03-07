interface ModuleEntry {
  id: string;
  title: string;
  routes: string[];
}

export function Menu({ user, path }: { user: string | null; path: string }) {
  const isHome = path === "/" || path === "";
  return (
    <>
      {!isHome && (
        <a className="btn" href="/">
          <span>🏠</span>
          <label>home</label>
        </a>
      )}
      {user ? (
        <>
          <a className="btn" href={`/${user}/`}>
            <span>😊</span>
            <label>me</label>
          </a>
          <a className="btn" href="/logout">
            <span>🚪</span>
            <label>logout</label>
          </a>
        </>
      ) : (
        <>
          <a className="btn" href="/login?ret=/">
            <span>🔑</span>
            <label>login</label>
          </a>
          <a className="btn" href="/register">
            <span>📝</span>
            <label>register</label>
          </a>
        </>
      )}
    </>
  );
}

export function Layout({ children, user, title, path, menuItems }: {
  children: React.ReactNode;
  user: string | null;
  title: string;
  path: string;
  menuItems?: React.ReactNode;
}) {
  return (<div>
    <label className="menu">
      <input
        name="functions"
        type="checkbox"
        style={{ display: "none" }}
      ></input>

      <span className="fixed top-0 right-0 z-10 p-2 flex items-center gap-4">
        <a className="menu-toggle bg-gray-50 border border-gray-200 flex items-center justify-center cursor-pointer text-base btn">⚙️</a>
      </span>

      <span className="functions flex-1 fixed right-0 z-20 h-full overflow-y-auto bg-gray-50 text-sm capitalize flex flex-col gap-2 p-4">
        <Menu user={user} path={path} />
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
