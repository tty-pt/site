import { Handlers, PageProps } from "$fresh/server.ts";
import { Layout } from "@/ssr/ui.tsx";
import type { State } from "#/routes/_middleware.ts";

interface PoemEditData {
  user: string | null;
  id: string;
  title: string;
}

export const handler: Handlers<PoemEditData, State> = {
  async POST(req, ctx) {
    const data = JSON.parse(await req.text());
    return ctx.render({
      user: ctx.state.user,
      id: ctx.params.id,
      title: data.title ?? "",
    });
  },
};

export default function PoemEdit({ data }: PageProps<PoemEditData>) {
  const { id, title } = data;

  return (
    <Layout user={data.user} title={`Edit ${title || id}`} path={`/poem/${id}/edit`}>
      <div class="center">
        <h1>Edit Poem</h1>
        <form
          method="POST"
          action={`/poem/${id}/edit`}
          encType="multipart/form-data"
        >
          <label>
            <span>Title:</span>
            <input type="text" name="title" defaultValue={title} />
          </label>
          <label>
            <span>File:</span>
            <input type="file" name="file" accept=".html,.htm,.txt" />
          </label>
          <div>
            <button type="submit">Save</button>
            <a href={`/poem/${id}`}>Cancel</a>
          </div>
        </form>
      </div>
    </Layout>
  );
}
