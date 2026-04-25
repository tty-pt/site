import { Handlers, PageProps } from "$fresh/server.ts";
import { IndexAdd } from "@/index/IndexAdd.tsx";
import type { State } from "#/routes/_middleware.ts";
import { loginRedirect } from "@/ssr/ui.tsx";

interface AddData {
  user: string | null;
  extraFields: { name: string; value: string }[];
}

export const handler: Handlers<AddData, State> = {
  GET(req, ctx) {
    if (!ctx.state.user) {
      return loginRedirect(req);
    }
    const choir = new URL(req.url).searchParams.get("choir") ?? "";
    const extraFields = choir ? [{ name: "choir", value: choir }] : [];
    return ctx.render({ user: ctx.state.user, extraFields });
  },
};

export default function SongbookAdd({ data }: PageProps<AddData>) {
  return <IndexAdd user={data.user} module="songbook" extraFields={data.extraFields} />;
}
