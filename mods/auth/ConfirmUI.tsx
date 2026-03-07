import { Layout } from "../ssr/ui.tsx";

export default function ConfirmUI({ user, path, params }: { user: string | null; path: string; params: Record<string,string> }) {
  const u = params.u || "";
  const r = params.r || "";
  const href = u && r ? `/confirm?u=${encodeURIComponent(u)}&r=${encodeURIComponent(r)}` : "/confirm";

  return (
    <Layout user={user} title="Confirm" path={path}>
      <div className="center">
        {u && r ? (
          <p>
            Click to confirm your account: <a href={href}>Link</a>
          </p>
        ) : (
          <p>Follow the link sent to your email to confirm your account.</p>
        )}
      </div>
    </Layout>
  );
}
