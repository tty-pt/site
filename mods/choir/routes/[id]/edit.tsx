import { Handlers, PageProps } from "$fresh/server.ts";
import { Field, FormActions, FormPage, moduleItemPath, moduleItemActionPath, readPostedForm } from "@/ssr/ui.tsx";
import type { State } from "#/routes/_middleware.ts";

interface ChoirEditData {
  user: string | null;
  id: string;
  title: string;
  formats: string[];
}

export const handler: Handlers<ChoirEditData, State> = {
  async POST(req, ctx) {
    const params = await readPostedForm(req);
    const id = params.get("id") ?? ctx.params.id;
    const title = params.get("title") ?? "";
    const formatStr = params.get("format") ?? "";
    const formats = formatStr.split("\n").map((f) => f.trim()).filter(Boolean);

    return ctx.render({ user: ctx.state.user, id, title, formats });
  },
};

export default function ChoirEdit({ data }: PageProps<ChoirEditData>) {
  const { user, id, title, formats } = data;
  const path = `/api/choir/${id}/edit`;
  const cancelHref = moduleItemPath("choir", id);

  return (
    <FormPage user={user} title={`Edit ${title}`} path={moduleItemActionPath("choir", id, "edit")} icon="🎶">
      <form
        method="POST"
        action={path}
        encType="multipart/form-data"
        className="flex flex-col gap-4 w-full max-w-lg"
      >
        <Field label="Choir Name:">
          <input
            type="text"
            name="title"
            defaultValue={title}
            required
            className="w-full"
          />
        </Field>
        <Field label="Song Formats (one per line):">
          <textarea
            name="format"
            rows={10}
            defaultValue={formats.join("\n")}
            className="w-full font-mono"
          />
        </Field>
        <FormActions cancelHref={cancelHref} />
      </form>
    </FormPage>
  );
}
