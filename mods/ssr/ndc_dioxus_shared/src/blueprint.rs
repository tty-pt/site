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
                            .field("author", Field::string("Author")),
                    )
                    .model(
                        "song_type",
                        Model::new().field("name", Field::string("Name")),
                    )
                    .model("poem", Model::new().field("title", Field::string("Title")))
                    .model("choir", Model::new().field("title", Field::string("Title")))
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
