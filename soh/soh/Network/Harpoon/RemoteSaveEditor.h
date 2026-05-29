#ifndef SOH_NETWORK_HARPOON_REMOTE_SAVE_EDITOR_H
#define SOH_NETWORK_HARPOON_REMOTE_SAVE_EDITOR_H
#ifdef __cplusplus

// =============================================================================
// HarpoonRemoteSaveEditor — GM-only "save editor for somebody else's save".
//
// Wire flow:
//   1) GM opens the window via HarpoonMenu → GM Controls → Remote Save Editor.
//   2) GM picks a target peer. Editor fires HARPOON.SAVE_PEEK_REQUEST with
//      { targetClientId, requesterClientId }.
//   3) Target peer (only if requester == host) snapshots its own gSaveContext
//      into a HarpoonTemplates::Template and answers with
//      HARPOON.SAVE_PEEK_RESPONSE { ownerClientId, requesterClientId,
//      <template fields> }.
//   4) GM caches the snapshot, draws the editor on top of the cached
//      Template (no live writes to anyone's gSaveContext yet).
//   5) GM clicks "Apply": editor reuses HarpoonTemplates::
//      BuildTemplateApplyPayload to broadcast the edited Template to the
//      target peer, who overwrites its save state via HandleTemplateApply.
//
// The peer side is dumb: it just answers peek requests from the host and
// already accepts TEMPLATE_APPLY. The editing UI lives entirely in the GM
// client. There is no per-edit network chatter — only the initial peek
// and a final apply.
// =============================================================================

#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>

#include "Templates.h"

namespace HarpoonRemoteSaveEditor {

class RemoteSaveEditorWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    void InitElement() override;
    void DrawElement() override;
    void UpdateElement() override {}
};

// Show the editor focused on a specific peer. Side effect: fires a
// SAVE_PEEK_REQUEST so the cached snapshot refreshes immediately.
void OpenForPeer(uint32_t targetClientId);

// Manually request a fresh snapshot for the currently-focused peer.
void RequestPeek(uint32_t targetClientId);

// Network handlers — called from Harpoon::HandlePacket_RoomEvent.
void HandlePeekRequest(const nlohmann::json& data);
void HandlePeekResponse(const nlohmann::json& data);

// Payload builders (exposed for tests / debugging).
nlohmann::json BuildPeekRequestPayload(uint32_t targetClientId);
nlohmann::json BuildPeekResponsePayload(uint32_t requesterClientId);

}  // namespace HarpoonRemoteSaveEditor

#endif  // __cplusplus
#endif  // SOH_NETWORK_HARPOON_REMOTE_SAVE_EDITOR_H
