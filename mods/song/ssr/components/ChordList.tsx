import { Layout } from "../../../ssr/ui.tsx";

export default function ChordList({ user, path, chords, titles = {} }: { user: string | null; path: string; chords: string[]; titles?: Record<string, string> }) {
  const buttons = chords.map((chord) => (
    <a key={chord} href={`/chords/${chord}/`} className="btn">
      {titles[chord] || chord}
    </a>
  ));

  return (
    <Layout user={user} title="chords" path={path}>
      <div className="center">
        {buttons.length > 0 ? (
          <div className="btn-row">{buttons}</div>
        ) : (
          <p>No chords yet.</p>
        )}
        <a href="/chords/add" className="btn">
          Add Chord
        </a>
      </div>
    </Layout>
  );
}
