import React from "https://esm.sh/react@18";

interface ModuleEntry {
  id: string;
  title: string;
  routes: string[];
  ssr: string;
  be?: string;
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

export function Layout({ children, user, title, path }: { children: React.ReactNode; user: string | null; title: string; path: string }) {
  return (
    <label className="menu">
      <input type="checkbox" style={{ display: "none" }} />
      <span className="fixed top-0 right-0 z-10 p-2 flex items-center gap-4">
        <span className="menu-toggle bg-gray-50 border border-gray-200 flex items-center justify-center cursor-pointer text-base">☰</span>
      </span>
      <span className="functions flex-1 fixed right-0 z-20 h-full overflow-y-auto bg-gray-50 text-sm capitalize flex flex-col gap-2 p-4">
        <h2 className="p-4">Menu</h2>
        <Menu user={user} path={path} />
      </span>
      <div className="main">
        <h1>{title}</h1>
        {children}
      </div>
    </label>
  );
}
