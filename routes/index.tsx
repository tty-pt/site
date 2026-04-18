import { Handlers, PageProps } from "$fresh/server.ts";
import { Layout } from "@/ssr/ui.tsx";
import type { State } from "./_middleware.ts";

interface HomeData {
  modules: ModuleEntry[];
  user: string | null;
  path: string;
}

// Handler uses middleware state instead of parsing headers directly
export const handler: Handlers<HomeData, State> = {
  async GET(req, ctx) {
    const url = new URL(req.url);
    
    return ctx.render({ 
      modules: ctx.state.modules, 
      user: ctx.state.user, 
      path: url.pathname 
    });
  }
};

export default function Home({ data }: PageProps<HomeData>) {
  const { modules, user, path } = data;
  
  const buttons = modules
    .filter((item) => Number(item.flags))
    .map((item) => (
      <a
        key={item.id}
        href={`/${item.id}/`}
        className="btn"
      >
        {item.title || item.id}
      </a>
    ));

  return (
    <Layout user={user} title="tty.pt" path={path}>
      <div className="center">
        {buttons}
      </div>
    </Layout>
  );
}
