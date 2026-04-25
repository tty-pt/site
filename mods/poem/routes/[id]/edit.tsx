import { Handlers, PageProps } from "$fresh/server.ts";
import { Field, FormActions, FormPage, moduleItemActionPath, moduleItemPath, readPostedJson } from "@/ssr/ui.tsx";
import type { State } from "#/routes/_middleware.ts";

interface PoemEditData {
  user: string | null;
  id: string;
  title: string;
}

export const handler: Handlers<PoemEditData, State> = {
  async POST(req, ctx) {
    const result = await readPostedJson<{ title?: string }>(req);
    const title = result.ok ? result.data.title ?? "" : "";
    return ctx.render({
      user: ctx.state.user,
      id: ctx.params.id,
      title,
    });
  },
};

export default function PoemEdit({ data }: PageProps<PoemEditData>) {
  const { id, title } = data;
  const path = moduleItemActionPath("poem", id, "edit");

  return (
    <FormPage user={data.user} title={`Edit ${title || id}`} path={path} icon="📜" heading="Edit Poem">
      <form method="POST" action={path} encType="multipart/form-data" className="flex flex-col gap-4">
        <Field label="Title:">
          <input type="text" name="title" defaultValue={title} />
        </Field>
        <Field label="File:">
          <input type="file" name="file" accept=".html,.htm,.txt" />
        </Field>
        <FormActions cancelHref={moduleItemPath("poem", id)} submitLabel="Save" />
      </form>
    </FormPage>
  );
}
