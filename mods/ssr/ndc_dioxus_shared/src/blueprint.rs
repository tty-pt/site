use std::sync::{Arc, OnceLock};

use hyle::{Blueprint, Field, FieldType, InputHint, Model, Reference};

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
                            .field(
                                "type",
                                Field::array(
                                    "Type",
                                    FieldType::Reference {
                                        reference: Reference {
                                            entity: "song.types".into(),
                                            display_field: "name".into(),
                                        },
                                    },
                                ),
                            )
                            .field("author", Field::string("Author"))
                            .field("yt", Field::string("YouTube URL").with_input(InputHint::new("url")))
                            .field("audio", Field::string("Audio URL").with_input(InputHint::new("url")))
                            .field("pdf", Field::string("PDF URL").with_input(InputHint::new("url")))
                            .field("data", Field::textarea("Chord Data", 20))
                            .field("owner", Field::string("Owner")),
                    )
                    .model(
                        "song.types",
                        Model::new().field("name", Field::string("Name")),
                    )
                    .model(
                        "poem",
                        Model::new()
                            .field("title", Field::string("Title"))
                            .field("body_content", Field::file("Body Content"))
                            .field("owner", Field::string("Owner")),
                    )
                    .model(
                        "songbook",
                        Model::new()
                            .field("title", Field::string("Title"))
                            .field("choir", Field::reference("Choir", "choir")),
                    )
                    .model(
                        "choir",
                        Model::new()
                            .field("title", Field::string("Choir Name"))
                            .field("format", Field::textarea("Song Formats", 10)),
                    )
            )
        })
        .clone()
}
