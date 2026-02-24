import React from "https://esm.sh/react@18";
import { Layout } from "../../../ssr/ui.tsx";

export default function ConfirmUI({ user, path, params }: { user: string | null; path: string; params: Record<string,string> }) {
  const u = params.u || "";
  const r = params.r || "";
  const href = u && r ? `/confirm?u=${encodeURIComponent(u)}&r=${encodeURIComponent(r)}` : "/confirm";

  return (
    <Layout user={user} title="Confirm" path={path}>
      <div className="flex flex-col items-center gap-2">
        {u && r ? (
          <p>
            Click to confirm your account: <a href={href} className="text-[#6e5f8a] hover:text-[#877cb3]">Link</a>
          </p>
        ) : (
          <p>Follow the link sent to your email to confirm your account.</p>
        )}
      </div>
    </Layout>
  );
}
