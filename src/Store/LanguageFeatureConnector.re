/*
 * LanguageFeatureConnector.re
 *
 * This implements an updater (reducer + side effects) for language features
 */

open EditorCoreTypes;
open Oni_Core;
open Oni_Model;
open Actions;
open Oni_Syntax;

module DefinitionResult = LanguageFeatures.DefinitionResult;
module Editor = Feature_Editor.Editor;

module Log = (val Log.withNamespace("Oni2.Store.LanguageFeatures"));

let start = () => {
  let checkForDefinitionEffect = (languageFeatures, buffer, location) =>
    Isolinear.Effect.createWithDispatch(
      ~name="languageFeature.checkForDefinition", dispatch => {
      Log.debug("Checking for definition...");

      let getDefinitionPromise =
        LanguageFeatures.requestDefinition(
          ~buffer,
          ~location,
          languageFeatures,
        );

      let getHighlightsPromise =
        LanguageFeatures.requestDocumentHighlights(
          ~buffer,
          ~location,
          languageFeatures,
        );

      let id = Buffer.getId(buffer);

      Lwt.on_success(getHighlightsPromise, result => {
        dispatch(
          BufferHighlights(
            BufferHighlights.DocumentHighlightsAvailable(id, result),
          ),
        )
      });

      Lwt.on_failure(getHighlightsPromise, _exn => {
        dispatch(
          BufferHighlights(BufferHighlights.DocumentHighlightsCleared(id)),
        )
      });

      Lwt.on_success(getDefinitionPromise, result =>
        dispatch(DefinitionAvailable(id, location, result))
      );
    });

  let findAllReferences = (state: State.t) =>
    Isolinear.Effect.createWithDispatch(
      ~name="languageFeature.findAllReferences", dispatch => {
      let maybeBuffer = state |> Selectors.getActiveBuffer;

      let editor = Feature_Layout.activeEditor(state.layout);

      Option.iter(
        buffer => {
          let location = Editor.getPrimaryCursor(editor);
          let promise =
            LanguageFeatures.requestFindAllReferences(
              ~buffer,
              ~location,
              state.languageFeatures,
            );

          Lwt.on_success(promise, result => {
            dispatch(References(References.Set(result)))
          });
        },
        maybeBuffer,
      );
    });

  let updater = (state: State.t, action) => {
    let default = (state, Isolinear.Effect.none);
    switch (action) {
    | References(References.Requested) => (state, findAllReferences(state))

    | References(References.Set(references)) => (
        {...state, references},
        Isolinear.Effect.none,
      )

    | Editor({msg: CursorsChanged(cursors), _})
        when Feature_Vim.mode(state.vim) != Vim.Types.Insert =>
      switch (Selectors.getActiveBuffer(state)) {
      | None => (state, Isolinear.Effect.none)
      | Some(buf) =>
        let location =
          switch (cursors) {
          | [cursor, ..._] => (cursor :> Location.t)
          | [] => Location.{line: Index.zero, column: Index.zero}
          };

        (
          state,
          checkForDefinitionEffect(state.languageFeatures, buf, location),
        );
      }

    | _ => default
    };
  };
  updater;
};
