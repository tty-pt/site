import IndexAdd from "@/index/IndexAdd.tsx";
import type { AddProps } from "@/index/IndexAdd.tsx"; 
export { handler } from "@/index/IndexAdd.tsx";

export default function SongAdd({ data }: AddProps) {
  return <IndexAdd user={data.user} module="poem" />;
}
