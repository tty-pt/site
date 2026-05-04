use std::sync::{Arc, OnceLock};

use hyle::{Blueprint, Field, Model};

static BLUEPRINT: OnceLock<Arc<Blueprint>> = OnceLock::new();

pub fn get_blueprint() -> Arc<Blueprint> {
    BLUEPRINT
        .get_or_init(|| {
            Arc::new(
                Blueprint::new()
                    .model(
                        "song",
                        Model::new()
                            .field("title", Field::string("Title"))
                            .field("type", Field::reference("Type", "song_type"))
                            .field("author", Field::string("Author"))
                            .field("yt", Field::string("YouTube URL"))
                            .field("audio", Field::string("Audio URL"))
                            .field("pdf", Field::string("PDF URL"))
                            .field("data", Field::textarea("Chord Data", 20)),
                    )
                    .model(
                        "song_type",
                        Model::new().field("name", Field::string("Name")),
                    )
                    .model(
                        "poem",
                        Model::new()
                            .field("title", Field::string("Title"))
                            .field("file", Field::file("File")),
                    )
                    .model(
                        "choir",
                        Model::new()
                            .field("title", Field::string("Choir Name"))
                            .field("format", Field::textarea("Song Formats", 10)),
                    )
                    .model(
                        "songbook",
                        Model::new()
                            .field("title", Field::string("Title"))
                            .field("choir", Field::reference("Choir", "choir_ref")),
                    )
                    .model(
                        "choir_ref",
                        Model::new().field("name", Field::string("Name")),
                    ),
            )
        })
        .clone()
}
