import { Handlers, PageProps } from "$fresh/server.ts";
import { Layout } from "../ssr/ui.tsx";
import type { State } from "#/routes/_middleware.ts";

interface DeleteData {
  user: string | null;
  module: string;
  id: string;
  title: string;
}

export const handler: Handlers<DeleteData, State> = {
  async POST(req, ctx) {
    const data = JSON.parse(await req.text());
    const splits = req.url.split("/");
    splits.pop(); // "delete"
    const id = splits.pop()!;
    const module = splits.pop()!;
    return ctx.render({
      user: ctx.state.user,
      module,
      id,
      title: data.title ?? "",
    });
  },
};

export default function IndexDelete({ data }: PageProps<DeleteData>) {
  const { module, id, title } = data;
  const path = `/${module}/${id}/delete`;

  return (
    <Layout user={data.user} title={`Delete ${title || id}`} path={path} icon="🏠">
      <div class="center">
        <p>Are you sure you want to delete <strong>{title || id}</strong>?</p>
        <form method="POST" action={path}>
          <button type="submit">Delete</button>
          <a href={`/${module}/${id}`}>Cancel</a>
        </form>
      </div>
    </Layout>
  );
}
