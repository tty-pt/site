import { Handlers, PageProps } from "$fresh/server.ts";
import { FormActions, FormPage, moduleItemActionPath, moduleItemPath, modulePath, readPostedForm } from "@/ssr/ui.tsx";
import type { State } from "#/routes/_middleware.ts";
import SongbookEditRow from "#/islands/SongbookEditRow.tsx";

interface Song {
	chordId: string;
	transpose: number;
  format: string;
  originalKey: number;
}

interface Chord {
  id: string;
  title: string;
  type: string;
}

interface SbEditData {
  user: string | null;
  songbook: Songbook | null;
  allChords: Chord[];
  allTypes: string[];
}

interface Songbook {
	id: string;
	title: string;
	choir: string;
	songs: Song[];
}

interface SongbookEditPayload {
	title: string;
	choir: string;
	songs: Song[];
	allChords: Chord[];
	allTypes: string[];
}

function SongbookNotFound({ user }: { user: string | null }) {
	return (
		<FormPage
			user={user}
			title="Songbook Not Found"
			path={modulePath("songbook")}
			icon="📖"
			heading="Songbook Not Found"
		>
			<></>
		</FormPage>
	);
}

function SongbookEditRows({
  songs,
  allChords,
  allTypes,
}: {
  songs: Song[];
  allChords: Chord[];
  allTypes: string[];
}) {
  return (
    <>
      {songs.map((song, index) => (
        <SongbookEditRow
          key={index}
          index={index}
          chordId={song.chordId}
          transpose={song.transpose}
          format={song.format}
          originalKey={song.originalKey}
          allChords={allChords}
          allTypes={allTypes}
        />
      ))}
    </>
  );
}

function SongbookEditActions({ songbookId }: { songbookId: string }) {
	return (
    <FormActions cancelHref={moduleItemPath("songbook", songbookId)}>
      <button type="submit" name="action" value="add_row" className="btn btn-action">
        + Add Row
      </button>
    </FormActions>
	);
}

function parseSong(line: string): Song {
	const [chordId = "", transpose = "0", format = "any", originalKey = "0"] = line.split(":");
	return {
		chordId,
		transpose: parseInt(transpose, 10),
		format,
		originalKey: parseInt(originalKey, 10),
	};
}

function parseJsonArray<T>(raw: string | null): T[] {
	if (!raw) return [];
	try {
		return JSON.parse(raw) as T[];
	} catch {
		return [];
	}
}

function parseBody(params: URLSearchParams): SongbookEditPayload {
	return {
		title: params.get("title") ?? "",
		choir: params.get("choir") ?? "",
		songs: (params.get("songs") ?? "")
			.split("\n")
			.filter((line) => line.trim())
			.map(parseSong),
		allChords: parseJsonArray<Chord>(params.get("allChords")),
		allTypes: parseJsonArray<string>(params.get("allTypes")),
	};
}

function toSongbook(id: string, payload: SongbookEditPayload): Songbook {
	return {
		id,
		title: payload.title,
		choir: payload.choir,
		songs: payload.songs,
	};
}

function SongbookEditForm({
	songbook,
	allChords,
	allTypes,
}: {
	songbook: Songbook;
	allChords: Chord[];
	allTypes: string[];
}) {
	return (
		<form
			method="POST"
			action={moduleItemActionPath("songbook", songbook.id, "edit")}
			encType="multipart/form-data"
			className="flex flex-col gap-2 w-full"
		>
			<SongbookEditRows songs={songbook.songs} allChords={allChords} allTypes={allTypes} />
			<input
				type="hidden"
				name="amount"
				value={`${songbook.songs.length}`}
			/>
			<SongbookEditActions songbookId={songbook.id} />
		</form>
	);
}

export const handler: Handlers<SbEditData, State> = {
	async POST(req, ctx) {
		const payload = parseBody(await readPostedForm(req));

		return ctx.render({
			user: ctx.state.user,
			songbook: toSongbook(ctx.params.id, payload),
			allChords: payload.allChords,
			allTypes: payload.allTypes,
		});
	},
};

export default function SbEdit({ data }: PageProps<SbEditData>) {
  if (!data.songbook) {
    return <SongbookNotFound user={data.user} />;
  }

	return (
		<FormPage
			user={data.user}
			title={`Edit ${data.songbook.title}`}
			path={moduleItemActionPath("songbook", data.songbook.id, "edit")}
			icon="📖"
			heading={`Edit ${data.songbook.title}`}
		>
			<SongbookEditForm
				songbook={data.songbook}
				allChords={data.allChords}
				allTypes={data.allTypes}
			/>
		</FormPage>
	);
}
