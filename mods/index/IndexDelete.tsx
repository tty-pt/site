import { Handlers, PageProps } from "$fresh/server.ts";
import { FormActions, Layout, moduleItemActionPath, moduleItemPath, readPostedJson } from "../ssr/ui.tsx";
import type { State } from "#/routes/_middleware.ts";

interface DeleteData {
  user: string | null;
  module: string;
  id: string;
  title: string;
}

export const handler: Handlers<DeleteData, State> = {
  async POST(req, ctx) {
    const result = await readPostedJson<{ title?: string }>(req);
    const splits = req.url.split("/");
    splits.pop(); // "delete"
    const id = splits.pop()!;
    const module = splits.pop()!;
    return ctx.render({
      user: ctx.state.user,
      module,
      id,
      title: result.ok ? result.data.title ?? "" : "",
    });
  },
};

export default function IndexDelete({ data }: PageProps<DeleteData>) {
  const { module, id, title } = data;
  const path = moduleItemActionPath(module, id, "delete");

  return (
    <Layout user={data.user} title={`Delete ${title || id}`} path={path} icon="🏠">
      <div class="center">
        <p>Are you sure you want to delete <strong>{title || id}</strong>?</p>
        <form method="POST" action={path}>
          <FormActions cancelHref={moduleItemPath(module, id)} submitLabel="Delete" />
        </form>
      </div>
    </Layout>
  );
}
