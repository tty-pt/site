import { Handlers, PageProps } from "$fresh/server.ts";
import { Field, FormActions, FormPage, moduleItemActionPath, moduleItemPath, readPostedForm } from "@/ssr/ui.tsx";
import type { State } from "#/routes/_middleware.ts";

interface SongEditData {
  user: string | null;
  id: string;
  title: string;
  type: string;
  yt: string;
  audio: string;
  pdf: string;
  data: string;
  author: string;
}

interface SongEditFormFieldsProps {
  title: string;
  author: string;
  type: string;
  yt: string;
  audio: string;
  pdf: string;
  songData: string;
}

function SongEditFormFields({
  title,
  author,
  type,
  yt,
  audio,
  pdf,
  songData,
}: SongEditFormFieldsProps) {
  return (
    <>
      <Field label="Title:">
        <input type="text" name="title" defaultValue={title} className="w-full" />
      </Field>

      <Field label="Author:">
        <input type="text" name="author" defaultValue={author} className="w-full" />
      </Field>

      <Field label="Type (e.g., entrada, santo, comunhao):">
        <textarea name="type" defaultValue={type} rows={3} className="w-full font-mono" />
      </Field>

      <Field label="YouTube URL:">
        <input type="text" name="yt" defaultValue={yt} className="w-full" />
      </Field>

      <Field label="Audio URL:">
        <input type="text" name="audio" defaultValue={audio} className="w-full" />
      </Field>

      <Field label="PDF URL:">
        <input type="text" name="pdf" defaultValue={pdf} className="w-full" />
      </Field>

      <Field label="Chord Data:">
        <textarea
          name="data"
          defaultValue={songData}
          rows={20}
          className="w-full font-mono whitespace-pre"
        />
      </Field>
    </>
  );
}

function parseBody(params: URLSearchParams): SongEditData {
  return {
    user: null,
    id: "",
    title: params.get("title") || "",
    type: params.get("type") || "",
    yt: params.get("yt") || "",
    audio: params.get("audio") || "",
    pdf: params.get("pdf") || "",
    data: params.get("data") || "",
    author: params.get("author") || "",
  };
}

export const handler: Handlers<SongEditData, State> = {
  async POST(req, ctx) {
    const data = parseBody(await readPostedForm(req));
    data.id = ctx.params.id;

    return ctx.render({
      ...data,
      user: ctx.state.user,
    });
  },
};

export default function SongEdit({ data }: PageProps<SongEditData>) {
  const { id, title, type, yt, audio, pdf, data: songData, author } = data;
  const action = moduleItemActionPath("song", id, "edit");
  const cancelHref = moduleItemPath("song", id);

  return (
    <FormPage user={data.user} title={`Edit ${title || id}`} path={moduleItemActionPath("song", id, "edit")} icon="🎸" heading="Edit Song">
      <form
        method="POST"
        action={action}
        encType="application/x-www-form-urlencoded"
        className="flex flex-col gap-4 w-full max-w-2xl"
      >
        <SongEditFormFields
          title={title}
          author={author}
          type={type}
          yt={yt}
          audio={audio}
          pdf={pdf}
          songData={songData}
        />
        <FormActions cancelHref={cancelHref} />
      </form>
    </FormPage>
  );
}
