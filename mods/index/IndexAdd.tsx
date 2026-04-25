import { Handlers, PageProps } from "$fresh/server.ts";
import { Field, FormActions, Layout, loginRedirect, moduleActionPath, moduleCollectionPath } from "../ssr/ui.tsx";
import type { State } from "#/routes/_middleware.ts";

interface ExtraField {
  name: string;
  value: string;
}

interface AddData {
  user: string | null;
  module: string;
  extraFields?: ExtraField[];
}

export const handler: Handlers<AddData, State> = {
  GET(req, ctx) {
    if (!ctx.state.user) {
      return loginRedirect(req);
    }
    const splits = req.url.split('/');
    splits.pop();
    const module = splits.pop()!;
    return ctx.render({
      user: ctx.state.user,
      module,
    });
  },
};

export function IndexAdd({
  module,
  user,
  extraFields,
}: { module?: string; user: string | null; extraFields?: ExtraField[] }) {
  const path = module ? moduleActionPath(module, "add") : "/add";
  const cancelHref = module ? moduleCollectionPath(module) : "/";

  return (
    <Layout user={user} title="Add Item" path={path} icon="🏠">
      <form action={path} method="POST" encType="multipart/form-data" className="flex flex-col gap-4">
        <Field label="Title:">
          <input name="title" />
        </Field>
        {extraFields?.map((f) => (
          <input key={f.name} type="hidden" name={f.name} value={f.value} />
        ))}
        <FormActions cancelHref={cancelHref} submitLabel="Add" />
      </form>
    </Layout>
  );
}

export default function IndexAddPage({ data }: PageProps<AddData>) {
  return <IndexAdd user={data.user} module={data.module} extraFields={data.extraFields} />;
}
