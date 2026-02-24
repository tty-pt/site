import React from "https://esm.sh/react@18";

export function Menu({ user, path }: { user: string | null; path: string }) {
  const isHome = path === "/" || path === "";
  return (
    <>
      {!isHome && (
        <a href="/" className="no-underline">
          <span>🏠</span>
          <label>home</label>
        </a>
      )}
      {user ? (
        <>
          <a href={`/${user}/`} className="no-underline">
            <span>😊</span>
            <label>me</label>
          </a>
          <a href="/logout" className="no-underline">
            <span>🚪</span>
            <label>logout</label>
          </a>
        </>
      ) : (
        <>
          <a href="/login?ret=/" className="no-underline">
            <span>🔑</span>
            <label>login</label>
          </a>
          <a href="/register" className="no-underline">
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
    <label className="flex flex-col h-screen m-0">
      <input type="checkbox" className="hidden" />
      <span className="p-2 flex items-center flex gap-4 fixed top-0 right-0 z-10">
        <div className="rounded-full w-4 h-4 flex items-center justify-center bg-gray-50 border border-gray-200 p-3 text-base">☰</div>
      </span>
      <span className="flex-1 text-sm capitalize flex flex-col fixed right-0 z-20 h-full overflow-y-auto bg-gray-50" id="menu">
        <h2 id="title" className="capitalize text-center">Title</h2>
        <Menu user={user} path={path} />
      </span>
      <div className="flex-1 p-4 flex flex-col items-center">
        <h2 className="capitalize text-center mb-2">{title}</h2>
        {children}
      </div>
    </label>
  );
}
