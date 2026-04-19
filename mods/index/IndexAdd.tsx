import { Handlers, PageProps } from "$fresh/server.ts";
import { Layout } from "../ssr/ui.tsx";
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
      const reqUrl = new URL(req.url);
      const forwardedHost = req.headers.get("X-Forwarded-Host");
      if (forwardedHost) {
        reqUrl.host = forwardedHost;
      }
      const ret = encodeURIComponent(reqUrl.pathname);
      return Response.redirect(new URL(`/auth/login?ret=${ret}`, reqUrl), 303);
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
  const path = `/${module}/add`;

  return (
    <Layout user={user} title="Add Item" path={path} icon="🏠">
      <form action={path} method="POST" encType="multipart/form-data">
        <label>
          <span>Title:</span>
          <input name="title" />
        </label>
        {extraFields?.map((f) => (
          <input key={f.name} type="hidden" name={f.name} value={f.value} />
        ))}
        <button type="submit">Add</button>
      </form>
    </Layout>
  );
}

export default function IndexAddPage({ data }: PageProps<AddData>) {
  return <IndexAdd user={data.user} module={data.module} extraFields={data.extraFields} />;
}
