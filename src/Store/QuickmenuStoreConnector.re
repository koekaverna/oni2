/*
 * QuickmenuStoreConnector.re
 *
 * This implements an updater (reducer + side effects) for the Quickmenu
 */
open Oni_Core;
open Oni_Model;
open Oni_UI;
open Utility;

module InputModel = Oni_Components.InputModel;
module ExtensionContributions = Exthost.Extension.Contributions;
module Command = Exthost.Types.Command;
module Menu = Exthost.Types.Menu;
module Selection = Oni_Components.Selection;

module Log = (val Log.withNamespace("Oni2.Store.Quickmenu"));

module Internal = {
  let prefixFor: Vim.Types.cmdlineType => string =
    fun
    | SearchForward => "/"
    | SearchReverse => "?"
    | Ex
    | Unknown => ":";

  let commandsToMenuItems = (commands, items) =>
    items
    |> List.map((item: Menu.item) =>
         Actions.{
           category: item.category,
           name: item.label,
           command: () =>
             switch (Command.Lookup.get(item.command, commands)) {
             | Some({msg: `Arg0(msg), _}) => msg
             | Some({msg: `Arg1(msgf), _}) => msgf(Json.Encode.null)
             | None => Actions.Noop
             },
           icon: item.icon,
           highlight: [],
         }
       )
    |> Array.of_list;

  let commandPaletteItems = (commands, menus, contextKeys) => {
    let contextKeys =
      WhenExpr.ContextKeys.union(
        contextKeys,
        WhenExpr.ContextKeys.fromList([
          (
            "oni.symLinkExists",
            WhenExpr.Value.(
              Sys.file_exists("/usr/local/bin/oni2") ? True : False
            ),
          ),
        ]),
      );

    Feature_Menus.commandPalette(contextKeys, commands, menus)
    |> commandsToMenuItems(commands);
  };
};

let start = (themeInfo: ThemeInfo.t) => {
  let selectItemEffect = (item: Actions.menuItem) =>
    Isolinear.Effect.createWithDispatch(~name="quickmenu.selectItem", dispatch => {
      let action = item.command();
      dispatch(action);
    });

  let executeVimCommandEffect =
    Isolinear.Effect.createWithDispatch(
      ~name="quickmenu.executeVimCommand", dispatch => {
      // TODO: Hard-coding "<CR>" and assuming `KeyboardInput` reaches vim seems very sketchy
      dispatch(
        Actions.KeyboardInput("<CR>"),
      )
    });

  let exitModeEffect =
    Isolinear.Effect.createWithDispatch(~name="quickmenu.exitMode", dispatch => {
      // TODO: Hard-coding "<ESC>" and assuming `KeyboardInput` reaches vim seems very sketchy
      dispatch(
        Actions.KeyboardInput("<ESC>"),
      )
    });

  let makeBufferCommands = (languageInfo, iconTheme, buffers) => {
    let workingDirectory = Rench.Environment.getWorkingDirectory(); // TODO: This should be workspace-relative

    buffers
    |> IntMap.to_seq
    |> Seq.map(snd)
    |> List.of_seq
    // Sort by most recerntly used
    |> List.fast_sort((a, b) =>
         - Float.compare(Buffer.getLastUsed(a), Buffer.getLastUsed(b))
       )
    |> List.filter_map(buffer => {
         let maybeName =
           Buffer.getMediumFriendlyName(~workingDirectory, buffer);
         let maybePath = Buffer.getFilePath(buffer);

         OptionEx.map2(
           (name, path) =>
             Actions.{
               category: None,
               name,
               command: () => {
                 Actions.OpenFileByPath(path, None, None);
               },
               icon: FileExplorer.getFileIcon(languageInfo, iconTheme, path),
               highlight: [],
             },
           maybeName,
           maybePath,
         );
       })
    |> Array.of_list;
  };

  let menuUpdater =
      (
        state: option(Quickmenu.t),
        action: Actions.t,
        buffers,
        languageInfo,
        iconTheme,
        themeInfo,
        commands,
        menus,
        contextKeys,
      )
      : (option(Quickmenu.t), Isolinear.Effect.t(Actions.t)) => {
    switch (action) {
    | QuickmenuShow(CommandPalette) => (
        Some({
          ...Quickmenu.defaults(CommandPalette),
          items: Internal.commandPaletteItems(commands, menus, contextKeys),
          focused: Some(0),
        }),
        Isolinear.Effect.none,
      )

    | QuickmenuShow(EditorsPicker) =>
      let items = makeBufferCommands(languageInfo, iconTheme, buffers);

      (
        Some({
          ...Quickmenu.defaults(EditorsPicker),
          items,
          focused: Some(min(1, Array.length(items) - 1)),
        }),
        Isolinear.Effect.none,
      );

    | QuickmenuShow(DocumentSymbols) => (
        Some({...Quickmenu.defaults(DocumentSymbols), focused: Some(0)}),
        Isolinear.Effect.none,
      )

    | QuickmenuShow(FilesPicker) => (
        Some({
          ...Quickmenu.defaults(FilesPicker),
          ripgrepProgress: Loading,
          focused: Some(0),
        }),
        Isolinear.Effect.none,
      )

    | QuickmenuShow(Wildmenu(cmdType)) => (
        Some({
          ...Quickmenu.defaults(Wildmenu(cmdType)),
          prefix: Some(Internal.prefixFor(cmdType)),
        }),
        Isolinear.Effect.none,
      )

    | QuickmenuShow(ThemesPicker) =>
      let items =
        ThemeInfo.getThemes(themeInfo)
        |> List.map((theme: ExtensionContributions.Theme.t) => {
             Actions.{
               category: Some("Theme"),
               name: theme.label,
               command: () => ThemeLoadByName(theme.label),
               icon: None,
               highlight: [],
             }
           })
        |> Array.of_list;

      (
        Some({...Quickmenu.defaults(ThemesPicker), items}),
        Isolinear.Effect.none,
      );

    | QuickmenuInput(key) => (
        Option.map(
          (Quickmenu.{query, selection, _} as state) => {
            let (text, selection) =
              InputModel.handleInput(~text=query, ~selection, key);

            Quickmenu.{...state, query: text, selection, focused: Some(0)};
          },
          state,
        ),
        Isolinear.Effect.none,
      )

    | QuickmenuInputClicked((newSelection: Selection.t)) => (
        Option.map(
          (Quickmenu.{variant, selection, _} as state) => {
            switch (variant) {
            | Wildmenu(_) =>
              let transition = selection.focus - newSelection.focus;

              if (transition > 0) {
                for (_ in 0 to transition) {
                  GlobalContext.current().dispatch(
                    Actions.KeyboardInput("<LEFT>"),
                  );
                };
              } else if (transition < 0) {
                for (_ in 0 downto transition) {
                  GlobalContext.current().dispatch(
                    Actions.KeyboardInput("<RIGHT>"),
                  );
                };
              };
            | _ => ()
            };

            Quickmenu.{...state, variant, selection: newSelection};
          },
          state,
        ),
        Isolinear.Effect.none,
      )

    | QuickmenuCommandlineUpdated(text, cursorPosition) => (
        Option.map(
          state =>
            Quickmenu.{
              ...state,
              query: text,
              selection: Selection.collapsed(~text, cursorPosition),
            },
          state,
        ),
        Isolinear.Effect.none,
      )

    | QuickmenuUpdateRipgrepProgress(progress) => (
        Option.map(
          (state: Quickmenu.t) => {...state, ripgrepProgress: progress},
          state,
        ),
        Isolinear.Effect.none,
      )

    | QuickmenuUpdateFilterProgress(items, progress) => (
        Option.map(
          (state: Quickmenu.t) => {
            let count = Array.length(items);
            {
              ...state,
              items,
              filterProgress: progress,
              focused: Option.map(min(count), state.focused),
            };
          },
          state,
        ),
        Isolinear.Effect.none,
      )

    | ListFocus(index) => (
        Option.map(
          (state: Quickmenu.t) => {
            let count = Array.length(state.items);

            {...state, focused: Some(IntEx.clamp(index, ~lo=0, ~hi=count))};
          },
          state,
        ),
        Isolinear.Effect.none,
      )

    | ListFocusUp => (
        Option.map(
          (state: Quickmenu.t) =>
            {
              ...state,
              focused:
                IndexEx.prevRollOverOpt(
                  state.focused,
                  ~last=Array.length(state.items) - 1,
                ),
            },
          state,
        ),
        Isolinear.Effect.none,
      )

    | ListFocusDown => (
        Option.map(
          (state: Quickmenu.t) =>
            {
              ...state,
              focused:
                IndexEx.nextRollOverOpt(
                  state.focused,
                  ~last=Array.length(state.items) - 1,
                ),
            },
          state,
        ),
        Isolinear.Effect.none,
      )

    | ListSelect =>
      switch (state) {
      | Some({variant: Wildmenu(_), _}) => (None, executeVimCommandEffect)

      | Some({items, focused: Some(focused), _}) =>
        switch (items[focused]) {
        | item => (None, selectItemEffect(item))
        | exception (Invalid_argument(_)) => (state, Isolinear.Effect.none)
        }

      | _ => (state, Isolinear.Effect.none)
      }

    | ListSelectBackground =>
      switch (state) {
      | Some({items, focused: Some(focused), _}) =>
        let eff =
          switch (items[focused]) {
          | item => selectItemEffect(item)
          | exception (Invalid_argument(_)) => Isolinear.Effect.none
          };
        (state, eff);

      | _ => (state, Isolinear.Effect.none)
      }

    | QuickmenuClose =>
      switch (state) {
      | Some({variant: Wildmenu(_), _}) => (None, exitModeEffect)
      | _ => (None, Isolinear.Effect.none)
      }

    | _ => (state, Isolinear.Effect.none)
    };
  };

  let updater = (state: State.t, action: Actions.t) => {
    let (menuState, menuEffect) =
      menuUpdater(
        state.quickmenu,
        action,
        state.buffers,
        state.languageInfo,
        state.iconTheme,
        themeInfo,
        State.commands(state),
        State.menus(state),
        WhenExpr.ContextKeys.fromSchema(ContextKeys.all, state),
      );

    ({...state, quickmenu: menuState}, menuEffect);
  };

  updater;
};

module QuickmenuFilterSubscription =
  FilterSubscription.Make({
    type item = Actions.menuItem;
    let format = Quickmenu.getLabel;
  });

let subscriptions = (ripgrep, dispatch) => {
  let (itemStream, addItems) = Isolinear.Stream.create();

  let filter = (query, items) => {
    QuickmenuFilterSubscription.create(
      ~id="quickmenu-filter",
      ~query,
      ~items=items |> Array.to_list, // TODO: This doesn't seem very efficient. Can Array.to_list be removed?
      ~itemStream,
      ~onUpdate=(items, ~progress) => {
        let items =
          items
          |> List.map((Filter.{item, highlight}) =>
               ({...item, highlight}: Actions.menuItem)
             )
          |> Array.of_list;
        Actions.QuickmenuUpdateFilterProgress(
          items,
          progress == 1. ? Complete : InProgress(progress),
        );
      },
    );
  };

  let documentSymbols = (languageFeatures, buffer) => {
    DocumentSymbolSubscription.create(
      ~id="document-symbols", ~buffer, ~languageFeatures, ~onUpdate=items => {
      addItems(items)
    });
  };

  let ripgrep = (languageInfo, iconTheme, configuration) => {
    let filesExclude =
      Configuration.getValue(c => c.filesExclude, configuration);
    let directory = Rench.Environment.getWorkingDirectory(); // TODO: This should be workspace-relative

    let stringToCommand = (languageInfo, iconTheme, fullPath) =>
      Actions.{
        category: None,
        name: Path.toRelative(~base=directory, fullPath),
        command: () => Actions.OpenFileByPath(fullPath, None, None),
        icon: FileExplorer.getFileIcon(languageInfo, iconTheme, fullPath),
        highlight: [],
      };

    RipgrepSubscription.create(
      ~id="workspace-search",
      ~filesExclude,
      ~directory,
      ~ripgrep,
      ~onUpdate=
        items => {
          items
          |> List.map(stringToCommand(languageInfo, iconTheme))
          |> addItems;

          dispatch(Actions.QuickmenuUpdateRipgrepProgress(Loading));
        },
      ~onComplete=() => Actions.QuickmenuUpdateRipgrepProgress(Complete),
    );
  };

  let updater = (state: State.t) => {
    switch (state.quickmenu) {
    | Some(quickmenu) =>
      switch (quickmenu.variant) {
      | CommandPalette
      | EditorsPicker
      | ThemesPicker => [filter(quickmenu.query, quickmenu.items)]

      | FilesPicker => [
          filter(quickmenu.query, quickmenu.items),
          ripgrep(state.languageInfo, state.iconTheme, state.configuration),
        ]

      | Wildmenu(_) => []
      | DocumentSymbols =>
        switch (Selectors.getActiveBuffer(state)) {
        | Some(buffer) => [
            filter(quickmenu.query, quickmenu.items),
            documentSymbols(state.languageFeatures, buffer),
          ]
        | None => []
        }
      }

    | None => []
    };
  };

  updater;
};
