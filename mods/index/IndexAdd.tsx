import { Handlers, PageProps } from "$fresh/server.ts";
import { Layout } from "../ssr/ui.tsx";
import type { State } from "#/routes/_middleware.ts";

interface AddData {
  user: string | null;
}

export const handler: Handlers<AddData, State> = {
  GET(req, ctx) {
    return ctx.render({ user: ctx.state.user });
  },
};

export default function IndexAdd({
  module,
  user,
}: { module: string, user: string | null }) {
  const path = `/${module}/add`;

  return (
    <Layout user={user} title="Add Item" path={path}>
      <form action={path} method="POST" encType="multipart/form-data">
        <label>
          <span>Title:</span>
          <input name="title" />
        </label>
        <button type="submit">Add</button>
      </form>
    </Layout>
  );
}

export type AddProps = PageProps<AddData>;
