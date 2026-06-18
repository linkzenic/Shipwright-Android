#include "randomizer.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <variables.h>
#include <macros.h>
#include <objects/gameplay_keep/gameplay_keep.h>
#include <functions.h>
#include <libultraship/libultraship.h>
#include <textures/icon_item_static/icon_item_static.h>
#include <textures/icon_item_24_static/icon_item_24_static.h>
#include "3drando/menu.hpp"
#include "soh/ResourceManagerHelpers.h"
#include "soh/SohGui/SohGui.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include "../../../src/overlays/actors/ovl_En_GirlA/z_en_girla.h"
#include "randomizer_check_objects.h"
#include <sstream>
#include <tuple>
#include "draw.h"
#include "soh/OTRGlobals.h"
#include <ship/window/FileDropMgr.h>
#include "static_data.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "trial.h"
#include "settings.h"
#include "soh/util.h"
#include "randomizerTypes.h"
#include "soh/Notification/Notification.h"
#include "soh/ObjectExtension/ObjectExtension.h"
#include "soh/Enhancements/randomizer/RCToRandInf.h"

// Extended Inventory for Custom Items (Page 2)
extern "C" {
#include "mods/extended_inventory.h"
#include "mods/extended_equipment.h"
#include "mods/items/custom_items.h"
#include "src/overlays/actors/ovl_Obj_Bean/z_obj_bean.h"

extern void func_80B8FE00(ObjBean*); // trigger planting
}

static ObjectExtension::Register<CheckIdentity> RegisterIdentity;

extern std::map<RandomizerCheckArea, std::string> rcAreaNames;

using json = nlohmann::json;
using namespace std::literals::string_literals;

std::unordered_map<std::string, RandomizerCheckArea> SpoilerfileAreaNameToEnum;
std::unordered_map<std::string, HintType> SpoilerfileHintTypeNameToEnum;
std::set<RandomizerCheck> excludedLocations;
std::set<RandomizerCheck> spoilerExcludedLocations;

bool generated;

// ============================================================================
// CUSTOM ITEMS RANDOMIZER MESSAGES
// ============================================================================
// Helper structure for custom item messages (defined inline to avoid linker issues)
struct CustomItemMessageEntry {
    s16 rgId;
    ItemID itemId;
    const char* english;
    const char* german;
    const char* french;
};

// Array of all 26 custom item messages
/* Custom Item Messages
 * Descriptions, Lore, and Translations provided by Gemini 3.0
 */
static const CustomItemMessageEntry customItemMessages[] = {
    // Movement Items
    // Skijer's progressive Roc's Feather (extended inventory page 2)
    { RG_PROGRESSIVE_ROCS, static_cast<ItemID>(ITEM_ROCS_FEATHER_SKIJER),
      "You got %rRoc's Feather%w!&This magical feather lets you&jump higher than normal.^Assign it to %y\xA1%w and "
      "press&to perform a high jump.&It even works in water!",
      "Du hast %rRocs Feder%w erhalten!&Diese magische Feder lässt&dich höher springen.^Weise sie %y\xA1%w zu und "
      "drücke&um hoch zu springen.&Funktioniert auch im Wasser!",
      "Vous obtenez la %rPlume de Roc%w!&Cette plume magique vous&permet de sauter plus haut.^Assignez-la à %y\xA1%w et "
      "appuyez&pour faire un grand saut.&Fonctionne même dans l'eau!" },

    // Vanilla rando Roc's Feather (shares the Nayru's Love slot, RSK_ROCS_FEATHER)
    { RG_ROCS_FEATHER, static_cast<ItemID>(ITEM_ROCS_FEATHER_SKIJER),
      "You got %rRoc's Feather%w!&Assign it to %y\xA1%w and press it&while standing to leap into&the air. It shares "
      "its slot&with Nayru's Love.",
      "Du hast %rRocs Feder%w erhalten!&Weise sie %y\xA1%w zu und drücke,&um in die Luft zu springen.&Sie teilt sich "
      "den Platz mit&Nayrus Umarmung.",
      "Vous obtenez la %rPlume de Roc%w!&Assignez-la à %y\xA1%w et appuyez&pour bondir dans les airs.&Elle partage son "
      "emplacement&avec l'Amour de Nayru." },

    { RG_ROCS_CAPE, static_cast<ItemID>(ITEM_ROCS_CAPE),
      "You got %rRoc's Cape%w!&This magical cape enhances&your jumping ability.^Now you can perform a&%gdouble jump%w "
      "in midair.&Press %y\xA1%w again while&jumping to go higher!",
      "Du hast %rRocs Umhang%w erhalten!&Dieser magische Umhang&verbessert deine Sprungkraft.^Du kannst nun "
      "einen&%gDoppelsprung%w in der Luft&ausführen. Drücke %y\xA1%w&erneut während du springst!",
      "Vous obtenez la %rCape de Roc%w!&Cette cape magique améliore&vos capacités de saut.^Vous pouvez maintenant "
      "effectuer&un %gdouble saut%w en l'air.&Appuyez sur %y\xA1%w en sautant&pour aller plus haut!" },

    { RG_DEKU_LEAF, static_cast<ItemID>(ITEM_DEKU_LEAF),
      "You got the %gDeku Leaf%w!&A giant leaf with powers&of the wind.^%yIn the air%w: Use it to glide&slowly and "
      "cover great&distances. Consumes magic.^%yOn the ground%w: Creates a gust&of wind that pushes objects&and "
      "enemies forward.",
      "Du hast das %gDeku-Blatt%w erhalten!&Ein Riesenblatt mit der&Kraft des Windes.^%yIn der Luft%w: Gleite "
      "langsam&und überbrücke große&Distanzen. Verbraucht Magie.^%yAm Boden%w: Erzeugt einen&Windstoß der Objekte "
      "und&Feinde nach vorne schiebt.",
      "Vous obtenez la %gFeuille Mojo%w!&Une feuille géante dotée&des pouvoirs du vent.^%yDans les airs%w: "
      "Planez&lentement sur de grandes&distances. Consomme de la magie.^%yAu sol%w: Crée une rafale&qui pousse les "
      "objets&et ennemis vers l'avant." },

    // Spell Items
    { RG_HYLIAS_GRACE, static_cast<ItemID>(ITEM_HYLIAS_GRACE),
      "You got %pHylia's Grace%w!&A divine blessing that transforms&you into a %cfairy%w for 10 seconds.^Press %y\xA1%w "
      "to activate&(requires a %rFairy in a Bottle%w).^%yA%w = Ascend  %yB%w = Descend&%yL%w = Sprint&1 minute "
      "cooldown after use.",
      "Du hast %pHylias Gnade%w erhalten!&Ein göttlicher Segen der dich&für 10 Sekunden in eine %cFee%w "
      "verwandelt.^Drücke %y\xA1%w zum Aktivieren&(benötigt eine %rFee in einer Flasche%w).^%yA%w = Aufsteigen  %yB%w = "
      "Absteigen&%yL%w = Sprinten&1 Minute Abklingzeit nach Nutzung.",
      "Vous obtenez la %pGrâce d'Hylia%w!&Une bénédiction divine qui vous&transforme en %cfée%w pendant 10 "
      "secondes.^Appuyez sur %y\xA1%w pour activer&(nécessite une %rFée en Bouteille%w).^%yA%w = Monter  %yB%w = "
      "Descendre&%yL%w = Sprint&1 minute de recharge après utilisation." },

    // MM Masks (Third Inventory Page)
    { RG_MM_MASK_POSTMAN, static_cast<ItemID>(ITEM_MM_MASK_POSTMAN),
      "You got the %yPostman's Hat%w!&The official cap of Termina's&most punctual courier.^Equip from the mask page.^Walk up to any %gunlocked mailbox%w&and press %y\xA0%w to open the&%cMailbox Warp Menu%w - fast travel&to any other unlocked mailbox.",
      "Du hast den %yBriefträgerhut%w!&Die offizielle Mütze von Terminas&pünktlichstem Boten.^Aufsetzen auf der Maskenseite.^Geh zu einem %gfreigeschalteten Briefkasten%w&und drücke %y\xA0%w für das&%cBriefkasten-Warp-Menü%w - Schnellreise&zu jedem anderen freigeschalteten Briefkasten.",
      "Vous obtenez le %yChapeau du Facteur%w!&Le képi officiel du courrier le&plus ponctuel de Termina.^Équipez depuis la page des masques.^Approchez n'importe quelle %gboîte aux&lettres débloquée%w et %y\xA0%w pour ouvrir&le %cMenu de Téléportation%w - voyage&rapide vers toute autre boîte." },
    { RG_MM_MASK_ALL_NIGHT, static_cast<ItemID>(ITEM_MM_MASK_ALL_NIGHT),
      "You got the %yAll-Night Mask%w!&A mask said to grant insomnia&and the gift of seeing in the dark.^Equip from the mask page.^While worn during %gdaytime%w, all&%cnight-only Gold Skulltulas%w spawn&as if it were night - Graveyard,&Zora's Fountain, Gerudo Fortress,&Kakariko, and Lon Lon Ranch.",
      "Du hast die %yNachtmaske%w!&Eine Maske, die Schlaflosigkeit&und Nachtsicht verleihen soll.^Aufsetzen auf der Maskenseite.^Beim Tragen am %gTag%w erscheinen alle&%cnur-nachts Goldskulltulas%w, als wäre&es Nacht - Friedhof, Zora-Quelle,&Gerudo-Festung, Kakariko und&Lon Lon Ranch.",
      "Vous obtenez le %yMasque de Nuit%w!&Un masque qui octroierait l'insomnie&et le don de voir dans l'obscurité.^Équipez depuis la page des masques.^Porté de %gjour%w, toutes les&%cSkulltulas d'Or de nuit%w apparaissent&comme s'il faisait nuit - Cimetière,&Fontaine Zora, Forteresse Gerudo,&Kakariko et Ranch Lon Lon." },
    { RG_MM_MASK_BLAST, static_cast<ItemID>(ITEM_MM_MASK_BLAST),
      "You got the %yBlast Mask%w!&A mask of explosive power born&of pure detonation.^Equip from the mask page.^%y\xA0%w detonates an %rinstant explosion%w&at Link's position - no bombs needed.&Cooldown: %g~310 frames%w (~16 s).&With %cgMods.BlastMask.Instant%w on,&cooldown drops to 1 frame.",
      "Du hast die %yExplosionsmaske%w!&Eine Maske explosiver Kraft,&geboren aus reiner Detonation.^Aufsetzen auf der Maskenseite.^%y\xA0%w zündet eine %rsofortige Explosion%w&an Links Position - keine Bomben nötig.&Abklingzeit: %g~310 Frames%w (~16 s).&Mit %cgMods.BlastMask.Instant%w an,&fällt die Abklingzeit auf 1 Frame.",
      "Vous obtenez le %yMasque d'Explosion%w!&Un masque de pure détonation&aux pouvoirs explosifs.^Équipez depuis la page des masques.^%y\xA0%w déclenche une %rexplosion instantanée%w&à la position de Link - aucune bombe.&Recharge: %g~310 frames%w (~16 s).&Avec %cgMods.BlastMask.Instant%w activé,&la recharge tombe à 1 frame." },
    { RG_MM_MASK_STONE, static_cast<ItemID>(ITEM_MM_MASK_STONE),
      "You got the %yStone Mask%w!&A featureless gray mask said to&render its wearer beneath notice.^Equip from the mask page.^While worn, %cenemies cannot see you%w&- they will not target you,&aggro you, or react to your&presence at all. Stealth pure.",
      "Du hast die %ySteinmaske%w!&Eine merkmallose graue Maske, die&ihren Träger unsichtbar macht.^Aufsetzen auf der Maskenseite.^Beim Tragen können dich %cFeinde&nicht sehen%w - sie zielen nicht&auf dich, werden nicht aggressiv&und reagieren nicht auf dich. Reine Tarnung.",
      "Vous obtenez le %yMasque de Pierre%w!&Un masque gris sans visage qui&rend son porteur invisible.^Équipez depuis la page des masques.^Pendant le port, %cles ennemis ne&peuvent pas vous voir%w - ils ne&vous ciblent pas, ne deviennent pas&agressifs, ne réagissent pas. Furtivité pure." },
    { RG_MM_MASK_GREAT_FAIRY, static_cast<ItemID>(ITEM_MM_MASK_GREAT_FAIRY),
      "You got the %yGreat Fairy Mask%w!&A wreath of long pink hair&blessed by the fairies.^Equip from the mask page.&In a fairy fountain, %y\xA0%w claims&the Great Fairy reward.^Press %y\xA1%w anywhere to open the&%cFairy Warp Menu%w - teleport to&any unlocked Great Fairy fountain.&Hair physics flow as you move.",
      "Du hast die %yFeenmaske%w!&Ein Kranz langer rosa Haare,&von den Feen gesegnet.^Aufsetzen auf der Maskenseite.&In einer Feenquelle %y\xA0%w drücken,&um die Belohnung zu erhalten.^Drücke %y\xA1%w überall für das&%cFeen-Warp-Menü%w - teleportiere&zu jeder freigeschalteten Feenquelle.&Haar-Physik beim Bewegen.",
      "Vous obtenez le %yMasque de la Grande Fée%w!&Une couronne de longs cheveux roses&bénie par les fées.^Équipez depuis la page des masques.&Dans une fontaine, %y\xA0%w réclame&la récompense de la Grande Fée.^%y\xA1%w n'importe où ouvre le&%cMenu de Téléportation%w - voyagez&vers toute fontaine débloquée.&Physique de cheveux en mouvement." },
    { RG_MM_MASK_DEKU, static_cast<ItemID>(ITEM_MM_MASK_DEKU),
      "You got the %gDeku Mask%w!&Holds the spirit of a fallen&Deku Scrub.^Equip from the mask page -&Link transforms into a small,&light Deku Scrub.^%y\xA0%w spin attack (pn_attack).&Hold %y\xA0%w to aim -> release fires&a %gbubble projectile%w (costs Magic).^Stand on a %gDeku Flower%w + %y\xA0%w to&burrow, charge, then launch into&a finite-distance %gglide%w.^%bWater%w skips you across the&surface like a stone (5 hops).&%rFire/lava/water%w is fatal.",
      "Du hast die %gDeku-Maske%w!&Birgt den Geist eines gefallenen&Deku-Höriger.^Aufsetzen auf der Maskenseite -&Link verwandelt sich in einen kleinen,&leichten Deku-Höriger.^%y\xA0%w Drehangriff (pn_attack).&Halte %y\xA0%w zum Zielen -> loslassen&feuert ein %gBlasenprojektil%w (Magie).^Auf einer %gDeku-Blume%w + %y\xA0%w zum&Eingraben, Aufladen und Abschuss&in einen begrenzten %gGleitflug%w.^%bWasser%w lässt dich wie ein Stein&hüpfen (5 Sprünge). %rFeuer/Lava/Wasser%w&ist tödlich.",
      "Vous obtenez le %gMasque Mojo%w!&Renferme l'esprit d'une Pestoène&tombée au combat.^Équipez depuis la page des masques -&Link se transforme en petite&Pestoène légère.^%y\xA0%w attaque tournoyante (pn_attack).&Maintenez %y\xA0%w pour viser -> relâchez&pour tirer une %gbulle%w (coûte de la Magie).^Sur une %gFleur Mojo%w + %y\xA0%w pour&s'enfouir, charger et se lancer&en %gvol plané%w à distance limitée.^%bL'eau%w vous fait ricocher comme&un caillou (5 sauts). %rFeu/lave/eau%w&est fatal." },
    { RG_MM_MASK_KEATON, static_cast<ItemID>(ITEM_MM_MASK_KEATON),
      "You got the %yKeaton Mask%w!&A fox-fairy mask said to summon&the trickster Keaton.^Equip from the mask page.&%rNo gameplay effect yet%w -&currently cosmetic only.",
      "Du hast die %yKeaton-Maske%w!&Eine Fuchsgeist-Maske, die den&Trickser Keaton beschwören soll.^Aufsetzen auf der Maskenseite.&%rNoch kein Effekt%w -&derzeit nur Kosmetik.",
      "Vous obtenez le %yMasque de Keaton%w!&Un masque de renard-esprit qui&invoquerait le farceur Keaton.^Équipez depuis la page des masques.&%rPas d'effet de jeu%w -&actuellement cosmétique seulement." },
    { RG_MM_MASK_BREMEN, static_cast<ItemID>(ITEM_MM_MASK_BREMEN),
      "You got the %yBremen Mask%w!&The mask of the marching musician&from the Bremen Town Musicians.^Equip from the mask page.&%rNo gameplay effect yet%w -&currently cosmetic only.",
      "Du hast die %yBremen-Maske%w!&Die Maske des marschierenden Musikers&aus den Bremer Stadtmusikanten.^Aufsetzen auf der Maskenseite.&%rNoch kein Effekt%w -&derzeit nur Kosmetik.",
      "Vous obtenez le %yMasque de Brême%w!&Le masque du musicien en marche&des Musiciens de Brême.^Équipez depuis la page des masques.&%rPas d'effet de jeu%w -&actuellement cosmétique seulement." },
    { RG_MM_MASK_BUNNY, static_cast<ItemID>(ITEM_MM_MASK_BUNNY),
      "You got the %yBunny Hood%w (MM)!&The fluffy long-eared hood of&Majora's Mask.^Equip from the mask page.^Grants %gincreased run speed%w&and %ghigher jumps%w (uses the&existing Bunny Hood enhancement&while wearing the MM hood).",
      "Du hast die %yHasenohren%w (MM)!&Die flauschige lange-Ohren-Mütze&aus Majoras Mask.^Aufsetzen auf der Maskenseite.^Gewährt %gerhöhte Laufgeschwindigkeit%w&und %ghöhere Sprünge%w (nutzt die&bestehende Hasenohren-Erweiterung&beim Tragen der MM-Mütze).",
      "Vous obtenez le %yMasque de Lapin%w (MM)!&La capuche aux longues oreilles&velues de Majora's Mask.^Équipez depuis la page des masques.^Octroie %gvitesse de course%w accrue&et %gsauts plus hauts%w (utilise&l'amélioration existante du Masque&de Lapin)." },
    { RG_MM_MASK_DON_GERO, static_cast<ItemID>(ITEM_MM_MASK_DON_GERO),
      "You got %yDon Gero's Mask%w!&The conductor's mask of the&frog choir.^Equip from the mask page.&Approach the %glog at Zora's River%w&and press %y\xA0%w to %ccollect every&unclaimed Frog Song reward%w at once.^Frog flags 0-4 = purple rupee&each, flags 5-6 = heart piece.",
      "Du hast %yDon Geros Maske%w!&Die Dirigentenmaske des&Froschchors.^Aufsetzen auf der Maskenseite.&Geh zum %gBaumstamm an Zoras Fluss%w&und drücke %y\xA0%w um %calle ungeholten&Froschlied-Belohnungen%w zu sammeln.^Flags 0-4 = je lila Rupie,&Flags 5-6 = Herzteil.",
      "Vous obtenez le %yMasque de Don Gero%w!&Le masque du chef d'orchestre&du chœur des grenouilles.^Équipez depuis la page des masques.&Approchez la %gbûche à la Rivière Zora%w&et appuyez sur %y\xA0%w pour %crécupérer&toutes les récompenses non-réclamées%w.^Drapeaux 0-4 = rubis violet chacun,&drapeaux 5-6 = pièce de cœur." },
    { RG_MM_MASK_SCENTS, static_cast<ItemID>(ITEM_MM_MASK_SCENTS),
      "You got the %yMask of Scents%w!&A mask said to grant the keen&nose of a beast.^Equip from the mask page.&%rNo gameplay effect yet%w -&currently cosmetic only.",
      "Du hast die %yGeruchsmaske%w!&Eine Maske, die den scharfen&Geruchssinn eines Tieres verleiht.^Aufsetzen auf der Maskenseite.&%rNoch kein Effekt%w -&derzeit nur Kosmetik.",
      "Vous obtenez le %yMasque des Odeurs%w!&Un masque qui octroierait le&flair d'une bête sauvage.^Équipez depuis la page des masques.&%rPas d'effet de jeu%w -&actuellement cosmétique seulement." },
    { RG_MM_MASK_GORON, static_cast<ItemID>(ITEM_MM_MASK_GORON),
      "You got the %rGoron Mask%w!&Holds the spirit of the fallen&Goron hero Darmani.^Equip from the mask page -&Link transforms into a heavy,&powerful Goron.^3-hit %rpunch combo%w (%y\xA0%w / %y\xA0%w / %y\xA0%w):&left fist, right fist, butt slam.&Same heavy blunt damage as the&%rMegaton Hammer%w.^Hold %y\xA3%w to %rcurl into a ball%w -&fast Goron Roll. %y\xA0%w mid-roll&to %rground pound%w (jump -> slam).^%rImmune to lava and fire%w.&%bSinks in water%w - voids out from&deep water.",
      "Du hast die %rGoronen-Maske%w!&Birgt den Geist des gefallenen&Goronen-Helden Darmani.^Aufsetzen auf der Maskenseite -&Link verwandelt sich in einen schweren,&kraftvollen Goronen.^3-Hit %rFaustkombo%w (%y\xA0%w / %y\xA0%w / %y\xA0%w):&linke Faust, rechte Faust, Sturzangriff.&Selber schwerer Schaden wie der&%rStahlhammer%w.^Halte %y\xA3%w zum %rEinrollen%w -&schneller Goronen-Roll. %y\xA0%w im Roll&für %rStampfangriff%w (Sprung -> Schlag).^%rImmun gegen Lava und Feuer%w.&%bSinkt im Wasser%w - Voids aus&tiefem Wasser.",
      "Vous obtenez le %rMasque de Goron%w!&Renferme l'esprit du héros&goron déchu Darmani.^Équipez depuis la page des masques -&Link se transforme en Goron&lourd et puissant.^%rCombo de 3 coups%w (%y\xA0%w / %y\xA0%w / %y\xA0%w):&poing gauche, poing droit, attaque-fesse.&Même dégâts lourds que la&%rMasse des Titans%w.^Maintenez %y\xA3%w pour vous %renrouler%w&en boule - Roulade Goron rapide.&%y\xA0%w en roulant pour un %rgroundpound%w&(saut -> impact).^%rImmunisé au feu et à la lave%w.&%bCoule dans l'eau%w - sortie forcée&en eau profonde." },
    { RG_MM_MASK_ROMANI, static_cast<ItemID>(ITEM_MM_MASK_ROMANI),
      "You got %yRomani's Mask%w!&A young rancher's mask carrying&her trust with cattle.^Equip from the mask page.&Walk up to %gany cow%w and press %y\xA0%w -&the cow gives you milk %cdirectly%w&without needing Epona's Song.",
      "Du hast %yRomanis Maske%w!&Eine junge Bauernmaske die ihr&Vertrauen zu Kühen trägt.^Aufsetzen auf der Maskenseite.&Geh zu %gjeder Kuh%w und drücke %y\xA0%w -&die Kuh gibt dir Milch %cdirekt%w&ohne Eponas Lied.",
      "Vous obtenez le %yMasque de Romani%w!&Le masque d'une jeune fermière&qui inspire confiance au bétail.^Équipez depuis la page des masques.&Approchez %gn'importe quelle vache%w et %y\xA0%w -&elle vous donne du lait %cdirectement%w&sans la Chanson d'Épona." },
    { RG_MM_MASK_CIRCUS_LEADER, static_cast<ItemID>(ITEM_MM_MASK_CIRCUS_LEADER),
      "You got the %yCircus Leader's Mask%w!&Worn, you become Ganondorf's&%cTax Collector%w. NPCs cower&and pay tribute on sight.^Talk to a minigame NPC and the&%centire minigame is skipped%w -&its reward is granted directly:^%gShooting Gallery%w (bullet bag/quiver),&%gBombchu Bowling%w (bomb bag -> heart piece),&%gIngo%w (Epona + Hyrule Field warp),&%gTalon%w (Milk Bottle, child Lon Lon),&%gAdult Malon%w (sells cow, %p100 Rupees%w),&%gHBA%w, %gFishing Pond%w, %gChest Game%w,&%gZora Diving Game%w (Silver Scale).^Repeat visits give a small bribe.&Rando-aware: delivers shuffled checks.",
      "Du hast die %yZirkusleitermaske%w!&Beim Tragen wirst du zu Ganondorfs&%cSteuereintreiber%w. NPCs zahlen&Tribut bei deinem Anblick.^Mit einem Minispiel-NPC reden und das&%ggesamte Minispiel wird übersprungen%w -&Belohnung wird direkt gegeben:^%gSchießbude%w (Munitionstasche/Köcher),&%gBombchu-Bowling%w (Bombentasche -> Herzteil),&%gIngo%w (Epona + Hyrule-Feld-Warp),&%gTalon%w (Milchflasche, Kind Lon Lon),&%gErwachsene Malon%w (verkauft Kuh, %p100 Rupien%w),&%gHBA%w, %gAngelteich%w, %gKistenspiel%w,&%gZora-Tauchspiel%w (Silberschuppe).^Wiederholungsbesuche geben Bestechungsgeld.&Rando-bewusst: liefert die geshufflten Items.",
      "Vous obtenez le %yMasque du Chef de Cirque%w!&Porté, vous devenez le %cCollecteur&d'Impôts%w de Ganondorf. Les PNJ&se soumettent à votre vue.^Parler à un PNJ de mini-jeu et le&%cmini-jeu entier est sauté%w -&sa récompense est donnée directement:^%gStand de Tir%w (sac à billes/carquois),&%gBombchu Bowling%w (sac de bombes -> cœur),&%gIngo%w (Épona + transition Plaine d'Hyrule),&%gTalon%w (Bouteille de Lait, Lon Lon enfant),&%gMalon adulte%w (vend vache, %p100 Rubis%w),&%gHBA%w, %gPêche%w, %gJeu de Coffres%w,&%gJeu de Plongée Zora%w (Écaille d'Argent).^Visites répétées donnent un pourboire.&Conscient du rando: livre les items shufflés." },
    { RG_MM_MASK_KAFEI, static_cast<ItemID>(ITEM_MM_MASK_KAFEI),
      "You got %yKafei's Mask%w!&A small mask carved in the&likeness of a missing groom.^Equip from the mask page.&While worn, %y\xA0%w toggles a&%cKafei character model%w on Link&(uses the N64_Kafei pak).&Cosmetic only.",
      "Du hast %yKafeis Maske%w!&Eine kleine Maske im Antlitz&eines verschwundenen Bräutigams.^Aufsetzen auf der Maskenseite.&Beim Tragen schaltet %y\xA0%w ein&%cKafei-Charaktermodell%w an Link um&(nutzt N64_Kafei pak).&Nur Kosmetik.",
      "Vous obtenez le %yMasque de Kafei%w!&Un petit masque sculpté à l'image&d'un fiancé disparu.^Équipez depuis la page des masques.&Pendant le port, %y\xA0%w bascule un&%cmodèle de Kafei%w sur Link&(utilise le pak N64_Kafei).&Cosmétique seulement." },
    { RG_MM_MASK_COUPLE, static_cast<ItemID>(ITEM_MM_MASK_COUPLE),
      "You got the %yCouple's Mask%w!&The reunion mask of two lovers&forever entwined.^Equip from the mask page.^%cPassive regen%w while worn:&%rDay%w -> %g+1 HP every 4 frames%w&(full hearts in ~32s).&%bNight%w -> %g+1 MP every 7 frames%w&(full magic in ~32s).",
      "Du hast die %yPaarmaske%w!&Die Wiedervereinigungsmaske zweier&für immer verbundener Liebender.^Aufsetzen auf der Maskenseite.^%cPassive Regeneration%w beim Tragen:&%rTag%w -> %g+1 HP alle 4 Frames%w&(volle Herzen in ~32s).&%bNacht%w -> %g+1 MP alle 7 Frames%w&(volle Magie in ~32s).",
      "Vous obtenez le %yMasque des Amoureux%w!&Le masque des retrouvailles de deux&amants à jamais entrelacés.^Équipez depuis la page des masques.^%cRégénération passive%w pendant le port:&%rJour%w -> %g+1 PV toutes les 4 frames%w&(cœurs pleins en ~32s).&%bNuit%w -> %g+1 PM toutes les 7 frames%w&(magie pleine en ~32s)." },
    { RG_MM_MASK_TRUTH, static_cast<ItemID>(ITEM_MM_MASK_TRUTH),
      "You got the %yMask of Truth%w (MM)!&The all-seeing eye that hears the&voices of beasts and stones.^Equip from the mask page.&%rNo gameplay effect yet%w -&currently cosmetic only.&(OOT's vanilla Mask of Truth is&a separate item.)",
      "Du hast die %yMaske der Wahrheit%w (MM)!&Das allsehende Auge, das die Stimmen&der Tiere und Steine hört.^Aufsetzen auf der Maskenseite.&%rNoch kein Effekt%w -&derzeit nur Kosmetik.&(OOTs Vanilla-Maske der Wahrheit&ist ein separates Item.)",
      "Vous obtenez le %yMasque de Vérité%w (MM)!&L'œil omniscient qui entend les&voix des bêtes et des pierres.^Équipez depuis la page des masques.&%rPas d'effet de jeu%w -&actuellement cosmétique seulement.&(Le Masque de Vérité OOT est&un objet distinct.)" },
    { RG_MM_MASK_ZORA, static_cast<ItemID>(ITEM_MM_MASK_ZORA),
      "You got the %bZora Mask%w!&Holds the spirit of the fallen&Zora guitarist Mikau.^Equip from the mask page -&Link transforms into a Zora,&master of the waters.^Real %bZora swim mechanics%w 1:1:&surface walk, %bfast dolphin swim%w,&%bswim dash%w (%y\xA0%w), %bdolphin jump%w arc.^On land: %y\xA0%w throws %bBoomerang Fins%w&(twin fin projectiles).&%y\xA3%w + %y\xA0%w raises an %cElectric Barrier%w&that shocks attackers (costs Magic).^Aerial %y\xA0%w -> flying %bjump kick%w.",
      "Du hast die %bZora-Maske%w!&Birgt den Geist des gefallenen&Zora-Gitarristen Mikau.^Aufsetzen auf der Maskenseite -&Link verwandelt sich in einen Zora,&Meister des Wassers.^Echte %bZora-Schwimmmechanik%w 1:1:&Wasserlauf, %bschnelles Delfinschwimmen%w,&%bSchwimm-Dash%w (%y\xA0%w), %bDelfinsprung%w-Bogen.^An Land: %y\xA0%w wirft %bBumerang-Flossen%w&(zwei Flossen-Projektile).&%y\xA3%w + %y\xA0%w erhebt eine %cElektrische Barriere%w&die Angreifer schockt (Magie).^In der Luft %y\xA0%w -> fliegender %bSprungkick%w.",
      "Vous obtenez le %bMasque de Zora%w!&Renferme l'esprit du guitariste&zora déchu Mikau.^Équipez depuis la page des masques -&Link se transforme en Zora,&maître des eaux.^Vraies %bmécaniques de nage Zora%w 1:1:&marche en surface, %bnage dauphin rapide%w,&%bdash de nage%w (%y\xA0%w), %bsaut de dauphin%w.^À terre: %y\xA0%w lance des %bAilerons Boomerang%w&(deux projectiles).&%y\xA3%w + %y\xA0%w élève une %cBarrière Électrique%w&qui foudroie les attaquants (Magie).^En l'air %y\xA0%w -> %bcoup de pied%w volant." },
    { RG_MM_MASK_KAMARO, static_cast<ItemID>(ITEM_MM_MASK_KAMARO),
      "You got %yKamaro's Mask%w!&The mask of a wandering ghost&dancer who lost his audience.^Equip from the mask page.&%cHold %y\xA0%w to dance%w (movement locks).&Release to stop.^At %gGoron City%w near Darunia,&dance with him for ~5 seconds&to trigger %cDarunia's Joy%w reward.",
      "Du hast %yKamaros Maske%w!&Die Maske eines umherwandernden Geist-&Tänzers ohne Publikum.^Aufsetzen auf der Maskenseite.&%cHalte %y\xA0%w zum Tanzen%w (Bewegung sperrt).&Loslassen zum Stoppen.^In %gGoronen-Stadt%w nahe Darunia,&tanze mit ihm für ~5 Sekunden,&um %cDarunias Freude%w auszulösen.",
      "Vous obtenez le %yMasque de Kamaro%w!&Le masque d'un fantôme danseur&errant ayant perdu son public.^Équipez depuis la page des masques.&%cMaintenez %y\xA0%w pour danser%w (mouvement verrouillé).&Relâchez pour arrêter.^Au %gVillage Goron%w près de Darunia,&dansez avec lui ~5 secondes pour&déclencher la %cJoie de Darunia%w." },
    { RG_MM_MASK_GIBDO, static_cast<ItemID>(ITEM_MM_MASK_GIBDO),
      "You got the %yGibdo Mask%w!&The decayed face of a mummy,&worn by ancient cult initiates.^Equip from the mask page.^While worn, %cReDeads and Gibdos%w&%gignore you completely%w -&they will not lunge or grab&while you wear the mask.",
      "Du hast die %yGibdo-Maske%w!&Das verwitterte Antlitz einer Mumie,&getragen von Kultanwärtern.^Aufsetzen auf der Maskenseite.^Beim Tragen %gignorieren%w dich&%cReDead und Gibdo%w komplett -&sie greifen dich nicht an und&packen dich nicht.",
      "Vous obtenez le %yMasque de Gibdo%w!&Le visage décrépit d'une momie,&porté par les initiés cultistes.^Équipez depuis la page des masques.^Pendant le port, %cReDead et Gibdo%w&%gvous ignorent complètement%w -&ils ne vous attaquent ni ne&vous attrapent." },
    { RG_MM_MASK_GARO, static_cast<ItemID>(ITEM_MM_MASK_GARO),
      "You got %yGaro's Mask%w!&The shrouded mask of a Garo&ninja, sworn to silence.^Equip from the mask page.&%rNo gameplay effect yet%w -&currently cosmetic only.",
      "Du hast %yGaros Maske%w!&Die verhüllte Maske eines Garo-&Ninjas, der Stille geschworen hat.^Aufsetzen auf der Maskenseite.&%rNoch kein Effekt%w -&derzeit nur Kosmetik.",
      "Vous obtenez le %yMasque de Garo%w!&Le masque drapé d'un ninja Garo,&qui a juré silence.^Équipez depuis la page des masques.&%rPas d'effet de jeu%w -&actuellement cosmétique seulement." },
    { RG_MM_MASK_CAPTAIN, static_cast<ItemID>(ITEM_MM_MASK_CAPTAIN),
      "You got the %yCaptain's Hat%w!&The crested helm of Captain&Keeta, leader of the Stalfos.^Equip from the mask page.^At night in %gHyrule Field%w only,&summons %rgiant Stalfos%w (adult Link)&or %rgiant Stalchildren%w (2x scale&and speed for child) to roam the field.&One spawns every ~5 seconds, max 3.",
      "Du hast den %yKapitänshut%w!&Der Kammhelm von Captain Keeta,&Anführer der Stalfos.^Aufsetzen auf der Maskenseite.^Nur nachts in %gHyrule-Feld%w werden&%rriesige Stalfos%w (Erwachsener) oder&%rriesige Stalchild%w (2x Größe und&Tempo für Jung) gerufen.&Einer alle ~5s, max. 3.",
      "Vous obtenez la %yCasquette du Capitaine%w!&Le casque du Capitaine Keeta,&chef des Stalfos.^Équipez depuis la page des masques.^La nuit dans la %gPlaine d'Hyrule%w,&invoque des %rStalfos géants%w (Link adulte)&ou des %rStalchild géants%w (2x taille&et vitesse pour Jeune Link).&Un toutes les ~5s, max. 3." },
    { RG_MM_MASK_GIANT, static_cast<ItemID>(ITEM_MM_MASK_GIANT),
      "You got the %yGiant's Mask%w!&The colossal mask said to grow&its wearer to monstrous size.^Equip from the mask page.&%rNo gameplay effect yet%w -&currently cosmetic only.&(In MM it scales Link to fight&Twinmold.)",
      "Du hast die %yRiesenmaske%w!&Die kolossale Maske, die ihren&Träger riesenhaft wachsen lässt.^Aufsetzen auf der Maskenseite.&%rNoch kein Effekt%w -&derzeit nur Kosmetik.&(In MM vergrößert sie Link&für den Twinmold-Kampf.)",
      "Vous obtenez le %yMasque de Géant%w!&Le masque colossal qui ferait&grandir son porteur.^Équipez depuis la page des masques.&%rPas d'effet de jeu%w -&actuellement cosmétique seulement.&(Dans MM, il agrandit Link pour&combattre Twinmold.)" },
    { RG_MM_MASK_FIERCE_DEITY, static_cast<ItemID>(ITEM_MM_MASK_FIERCE_DEITY),
      "You got %pFierce Deity's Mask%w!&The legendary forbidden mask of&a god-like warrior.^Equip from the mask page -&Link transforms into the towering&%pFierce Deity%w. The Final Form.^%c1.5x movement speed%w - the only&form with a speed multiplier.^Wields a massive %ptwo-handed sword%w&(PLAYER_ANIMTYPE_3 stance).&Every full-health %y\xA0%w swing fires&a long-range %psword beam%w projectile.^Hyrule's strongest combat form.",
      "Du hast %pMajoras Maske%w!&Die legendäre verbotene Maske eines&gottgleichen Kriegers.^Aufsetzen auf der Maskenseite -&Link verwandelt sich in den hoch&aufragenden %pFinsteren Gott%w. Die Endform.^%c1,5x Bewegungsgeschwindigkeit%w - die&einzige Form mit Geschwindigkeitsbonus.^Führt ein massives %pZweihandschwert%w&(PLAYER_ANIMTYPE_3-Haltung).&Jeder %y\xA0%w-Schwung bei voller Gesundheit&feuert einen %pSchwertstrahl%w in die Ferne.^Hyrules stärkste Kampfform.",
      "Vous obtenez le %pMasque du Dieu Féroce%w!&Le masque légendaire interdit d'un&guerrier divin.^Équipez depuis la page des masques -&Link se transforme en imposant&%pDieu Féroce%w. La Forme Finale.^%c1,5x vitesse de déplacement%w - la&seule forme avec un bonus de vitesse.^Manie une massive %pépée à deux mains%w&(posture PLAYER_ANIMTYPE_3).&Chaque coup %y\xA0%w à pleine santé tire&un %prayon d'épée%w à longue portée.^La forme de combat la plus puissante." },

    { RG_ZONAI_PERMAFROST, static_cast<ItemID>(ITEM_ZONAI_PERMAFROST),
      "You got %cZonai Permafrost%w!&Ancient Zonai technology that&freezes the flow of time itself.^Press %y\xA1%w to "
      "cast the spell.&%rAll enemies%w, %ypuzzle elements%w,&and even the %cday/night cycle%w&freeze for %g10 "
      "seconds%w!^Costs %g12 Magic%w per use.&Move freely while time is stopped.",
      "Du hast %cSonau Permafrost%w!&Uralte Sonau-Technologie die&den Fluss der Zeit einfriert.^Drücke %y\xA1%w um den "
      "Zauber&zu wirken. %rAlle Feinde%w,&%yRätsel-Elemente%w, und sogar&der %cTag/Nacht-Zyklus%w frieren&für %g10 "
      "Sekunden%w ein!^Kostet %g12 Magie%w pro Nutzung.&Bewege dich frei während die&Zeit angehalten ist.",
      "Vous obtenez %cPermafrost Soneau%w!&Technologie ancienne des Soneau&qui gèle le flux du temps.^Appuyez sur "
      "%y\xA1%w pour lancer&le sort. %rTous les ennemis%w,&%yéléments de puzzle%w, et même&le %ccycle jour/nuit%w "
      "gèlent&pendant %g10 secondes%w!^Coûte %g12 Magie%w par utilisation.&Bougez librement pendant que&le temps est "
      "arrêté." },

    { RG_DEMISE_DESTRUCTION, static_cast<ItemID>(ITEM_DEMISE_DESTRUCTION),
      "You got %rDemise Destruction%w!&The dark power of the Demon King&Demise, sealed in this artifact.^Press %y\xA1%w "
      "to unleash a&devastating %rlightning explosion%w&that damages all enemies in&a %glarge radius%w around "
      "you.^%rHigh Magic cost%w.&Best saved for emergencies!&The ground itself trembles...",
      "Du hast %rTodbringer Zerstörung%w!&Die dunkle Macht des Dämonenkönigs&Todbringer, versiegelt in "
      "diesem&Artefakt.^Drücke %y\xA1%w um eine verheerende&%rBlitz-Explosion%w zu entfesseln&die alle Feinde in "
      "einem&%ggroßen Radius%w um dich trifft.^%rHohe Magiekosten%w.&Am besten für Notfälle aufheben!&Der Boden selbst "
      "bebt...",
      "Vous obtenez %rDestruction de l'Avatar%w!&Le pouvoir sombre du Roi Démon&Avatar, scellé dans cet "
      "artefact.^Appuyez sur %y\xA1%w pour déchaîner&une %rexplosion de foudre%w&dévastatrice qui blesse tous "
      "les&ennemis dans un %glarge rayon%w.^%rCoût élevé en Magie%w.&À garder pour les urgences!&La terre elle-même "
      "tremble..." },

    { RG_TIME_GATE, static_cast<ItemID>(ITEM_TIME_GATE),
      "You got the %cTime Gate%w!&A portable door through the ages,&the power of the Temple of Time&in your "
      "hands.^Press %y\xA1%w to activate.&A prompt will ask: %g\"Travel&through time?\"%w^Select %yYes%w to switch "
      "between&%rChild%w and %gAdult%w Link&anywhere in the world!^Costs %g48 Magic%w per use.",
      "Du hast das %cZeittor%w!&Eine tragbare Tür durch die Zeit,&die Macht des Zeitturms in&deinen Händen.^Drücke "
      "%y\xA1%w zum Aktivieren.&Eine Frage erscheint: %g\"Durch&die Zeit reisen?\"%w^Wähle %yJa%w um zwischen&%rKind%w "
      "und %gErwachsenem%w Link&überall zu wechseln!^Kostet %g48 Magie%w pro Nutzung.",
      "Vous obtenez la %cPorte du Temps%w!&Une porte portable à travers les&âges, le pouvoir du Temple du Temps&dans "
      "vos mains.^Appuyez sur %y\xA1%w pour activer.&Une question apparaît: %g\"Voyager&dans le temps?\"%w^Sélectionnez "
      "%yOui%w pour passer&entre Link %rEnfant%w et %gAdulte%w&n'importe où!^Coûte %g48 Magie%w par utilisation." },

    // Tool Items
    { RG_SWITCH_HOOK, static_cast<ItemID>(ITEM_SWITCH_HOOK),
      "You got the %cSwitch Hook%w!&A magical hook that swaps&your position with targets.^Hold %y\xA1%w to aim,&release "
      "to fire.&%c\xA5%w = First-person mode^Swap places with pots, crates,&and certain enemies!&Non-swappable targets "
      "take damage.",
      "Du hast den %cWechselhaken%w!&Ein magischer Haken der deine&Position mit Zielen tauscht.^Halte %y\xA1%w zum "
      "Zielen,&lass los zum Feuern.&%c\xA5%w = Erste-Person^Tausche Plätze mit Töpfen, Kisten&und bestimmten "
      "Feinden!&Nicht-tauschbare Ziele nehmen Schaden.",
      "Vous obtenez le %cCrochet Échange%w!&Un crochet magique qui échange&votre position avec les cibles.^Maintenez "
      "%y\xA1%w pour viser,&relâchez pour tirer.&%c\xA5%w = Première personne^Échangez avec des pots, caisses,&et "
      "certains ennemis!&Les cibles non-échangeables subissent des dégâts." },

    { RG_MOGMA_MITTS, static_cast<ItemID>(ITEM_MOGMA_MITTS),
      "You got the %yMogma Mitts%w!&Claws of the underground.&Climb any wall! Uses %gMagic%w.",
      "Du hast die %yMogma-Klauen%w erhalten!&Klauen aus dem Untergrund.&Klettere überall! Verbraucht %gMagie%w.",
      "Vous obtenez les %yGants Mogma%w!&Griffes souterraines.&Grimpez partout! Utilise de la %gMagie%w." },

    { RG_GUST_JAR, static_cast<ItemID>(ITEM_GUST_JAR),
      "You got the %gGust Jar%w!&A vessel containing&ancient winds.^%ySuction mode%w: Hold %y\xA1%w&to absorb objects, "
      "enemies&and environmental elements.^%yCapture mode%w: Absorb fire,&ice or electricity to store&special "
      "ammunition.^%yShoot mode%w: Release %y\xA1%w to&fire what you captured.&%c\xA5%w = First-person mode",
      "Du hast den %gMagischen Krug%w!&Ein Gefäß mit uralten&Winden.^%yAnsaugmodus%w: Halte %y\xA1%w&um Objekte, Feinde "
      "und&Umgebungselemente anzusaugen.^%yFangmodus%w: Sauge Feuer,&Eis oder Elektrizität auf&als spezielle "
      "Munition.^%ySchussmodus%w: Lass %y\xA1%w los&um das Gefangene zu feuern.&%c\xA5%w = Erste-Person",
      "Vous obtenez le %gPot Magique%w!&Un récipient contenant&des vents anciens.^%yMode aspiration%w: Maintenez "
      "%y\xA1%w&pour absorber objets, ennemis&et éléments environnementaux.^%yMode capture%w: Absorbez feu,&glace ou "
      "électricité comme&munition spéciale.^%yMode tir%w: Relâchez %y\xA1%w pour&tirer ce que vous avez "
      "capturé.&%c\xA5%w = Première personne" },

    { RG_SHOVEL, static_cast<ItemID>(ITEM_SHOVEL),
      "You got the %yShovel%w!&A reliable tool for&excavation.^Use %y\xA1%w on soft soil&to dig and find "
      "hidden&treasures.^It can also reveal secret&%gGrottos%w and damage&buried enemies!",
      "Du hast die %ySchaufel%w!&Ein zuverlässiges Werkzeug&zum Graben.^Benutze %y\xA1%w auf weichem&Boden um zu graben "
      "und&verborgene Schätze zu finden.^Sie kann auch geheime&%gGrotten%w aufdecken und&vergrabene Feinde verletzen!",
      "Vous obtenez la %yPelle%w!&Un outil fiable pour&l'excavation.^Utilisez %y\xA1%w sur terre&meuble pour creuser "
      "et&trouver des trésors cachés.^Elle peut aussi révéler des&%gGrottes secrètes%w et blesser&les ennemis "
      "enterrés!" },

    // Weapon Items
    { RG_BALL_AND_CHAIN, static_cast<ItemID>(ITEM_BALL_AND_CHAIN),
      "You got the %yBall and Chain%w!&A heavy weapon from the&snow palace.^Hold %y\xA1%w to charge,&release to "
      "throw.&Crush ice and enemies!^With %g\xA4%w it homes in&on the enemy automatically.&Breaks %rRed "
      "Ice%w!^%rNote%w: Your speed is reduced&while it's equipped.",
      "Du hast die %yKettenkugel%w!&Eine schwere Waffe aus dem&Schneepalast.^Halte %y\xA1%w zum Aufladen,&lass los zum "
      "Werfen.&Zerschmettere Eis und Feinde!^Mit %g\xA4%w verfolgt sie&automatisch den Feind.&Zerbricht %rRotes "
      "Eis%w!^%rHinweis%w: Deine Geschwindigkeit&ist reduziert während sie&ausgerüstet ist.",
      "Vous obtenez le %yBoulet%w!&Une arme lourde du palais&des neiges.^Maintenez %y\xA1%w pour charger,&relâchez pour "
      "lancer.&Écrasez glace et ennemis!^Avec %g\xA4%w il suit&automatiquement l'ennemi.&Brise la %rGlace "
      "Rouge%w!^%rNote%w: Votre vitesse est réduite&tant qu'il est équipé." },

    { RG_WHIP, static_cast<ItemID>(ITEM_WHIP),
      "You got the %yWhip%w!&A versatile tool for combat&and exploration.^Press %y\xA1%w to lash forward.&It latches "
      "onto beams and bars&for pendulum swinging.^%ySwinging%w: Use the stick to&control the pendulum.&Release to "
      "launch with momentum!^%yCombat%w: Paralyze enemies,&pull shields, and disarm.&Also grabs items!",
      "Du hast die %yPeitsche%w!&Ein vielseitiges Werkzeug für&Kampf und Erkundung.^Drücke %y\xA1%w zum Schlagen.&Hakt "
      "sich an Balken und Stangen&zum Pendelschwingen ein.^%ySchwingen%w: Nutze den Stick um&das Pendel zu "
      "steuern.&Lass los für Schwung-Start!^%yKampf%w: Lähme Feinde,&ziehe Schilde weg und entwaffne.&Greift auch "
      "Items!",
      "Vous obtenez le %yFouet%w!&Un outil polyvalent pour le combat&et l'exploration.^Appuyez sur %y\xA1%w pour "
      "fouetter.&S'accroche aux poutres et barres&pour se balancer en pendule.^%yBalancement%w: Utilisez le stick&pour "
      "contrôler le pendule.&Relâchez pour vous lancer!^%yCombat%w: Paralysez les ennemis,&tirez les boucliers et "
      "désarmez.&Attrape aussi des objets!" },

    { RG_SPINNER, static_cast<ItemID>(ITEM_SPINNER),
      "You got the %ySpinner%w!&Ancient technology from the&desert sands.^Press %y\xA1%w to ride it&and glide around. "
      "Use it to&cross great distances.^With %g\xA4%w you perform&a homing attack towards&the enemy. Breaks rocks!",
      "Du hast den %yKreisel%w!&Uralte Technologie aus dem&Wüstensand.^Drücke %y\xA1%w um aufzusteigen&und zu gleiten. "
      "Überbrücke&große Distanzen damit.^Mit %g\xA4%w führst du einen&Verfolgungs-Angriff auf&den Feind aus. "
      "Zerbricht Felsen!",
      "Vous obtenez la %yToupie%w!&Technologie ancienne des&sables du désert.^Appuyez sur %y\xA1%w pour monter&et "
      "glisser. Utilisez-la pour&traverser de grandes distances.^Avec %g\xA4%w vous effectuez&une attaque guidée "
      "vers&l'ennemi. Brise les rochers!" },

    { RG_BOMB_ARROWS, static_cast<ItemID>(ITEM_BOMB_ARROWS),
      "You got %rBomb Arrows%w!&An explosive combination.^Requires %yArrows%w and %rBombs%w.&Use %y\xA1%w to enter "
      "first-person&mode and aim.^The arrow explodes on impact.&Consumes %y1 arrow%w + %r1 bomb%w&per shot.",
      "Du hast %rBombenpfeile%w!&Eine explosive Kombination.^Benötigt %yPfeile%w und %rBomben%w.&Benutze %y\xA1%w für "
      "Erste-Person&Modus und zielen.^Der Pfeil explodiert beim&Aufprall. Verbraucht %y1 Pfeil%w&+ %r1 Bombe%w pro "
      "Schuss.",
      "Vous obtenez les %rFlèches-Bombes%w!&Une combinaison explosive.^Nécessite des %yFlèches%w et "
      "%rBombes%w.&Utilisez %y\xA1%w pour entrer en&première personne et viser.^La flèche explose à l'impact.&Consomme "
      "%y1 flèche%w + %r1 bombe%w&par tir." },

    // Elemental Rods
    { RG_FIRE_ROD, static_cast<ItemID>(ITEM_ROD_FIRE),
      "You got the %rFire Rod%w!&A magical weapon that channels&the power of fire.^%yBasic attacks%w:&Slash = 3 "
      "fireballs&Stab = 1 fireball&Jump = Flamethrower down^%ySpecial attacks%w:&Spin = Expanding fire wave&Hold "
      "%y\xA1%w = Charge attack&%c\xA5%w = First-person mode^%rWarning%w: Without magic, the&fire will burn YOU. Make "
      "sure&you have enough magic!",
      "Du hast den %rFeuerstab%w!&Eine magische Waffe mit der&Kraft des Feuers.^%yBasisangriffe%w:&Hieb = 3 "
      "Feuerbälle&Stoß = 1 Feuerball&Sprung = Flammenwerfer^%ySpezialangriffe%w:&Wirbelattacke = Feuerwelle&Halte "
      "%y\xA1%w = Aufladen&%c\xA5%w = Erste-Person^%rWarnung%w: Ohne Magie verbrennt&das Feuer DICH. Achte auf&genug "
      "Magie!",
      "Vous obtenez la %rBaguette de Feu%w!&Une arme magique qui canalise&le pouvoir du feu.^%yAttaques de "
      "base%w:&Taille = 3 boules de feu&Estoc = 1 boule de feu&Saut = Lance-flammes^%yAttaques spéciales%w:&Tourbillon "
      "= Vague de feu&Maintenez %y\xA1%w = Charge&%c\xA5%w = Première personne^%rAttention%w: Sans magie, le feu&VOUS "
      "brûlera. Assurez-vous&d'avoir assez de magie!" },

    { RG_ICE_ROD, static_cast<ItemID>(ITEM_ROD_ICE),
      "You got the %bIce Rod%w!&A magical weapon that channels&the power of ice.^%yBasic attacks%w:&Slash = 3 ice "
      "projectiles&Stab = 1 ice projectile&Jump = Freezing blast down^%ySpecial attacks%w:&Spin = Expanding ice "
      "wave&Hold %y\xA1%w = Charge attack&%c\xA5%w = First-person mode^%rWarning%w: Without magic, the&ice will freeze "
      "YOU. Make sure&you have enough magic!",
      "Du hast den %bEisstab%w!&Eine magische Waffe mit der&Kraft des Eises.^%yBasisangriffe%w:&Hieb = 3 "
      "Eisprojektile&Stoß = 1 Eisprojektil&Sprung = Eisstrahl^%ySpezialangriffe%w:&Wirbelattacke = Eiswelle&Halte "
      "%y\xA1%w = Aufladen&%c\xA5%w = Erste-Person^%rWarnung%w: Ohne Magie friert&das Eis DICH ein. Achte auf&genug "
      "Magie!",
      "Vous obtenez la %bBaguette de Glace%w!&Une arme magique qui canalise&le pouvoir de la glace.^%yAttaques de "
      "base%w:&Taille = 3 projectiles de glace&Estoc = 1 projectile de glace&Saut = Souffle glacial^%yAttaques "
      "spéciales%w:&Tourbillon = Vague de glace&Maintenez %y\xA1%w = Charge&%c\xA5%w = Première "
      "personne^%rAttention%w: Sans magie, la glace&VOUS gèlera. Assurez-vous&d'avoir assez de magie!" },

    { RG_LIGHT_ROD, static_cast<ItemID>(ITEM_ROD_LIGHT),
      "You got the %yLight Rod%w!&A magical weapon that channels&the power of lightning.^%yBasic attacks%w:&Slash = 3 "
      "lightning bolts&Stab = 1 lightning bolt&Jump = Electric discharge^%ySpecial attacks%w:&Spin = Expanding "
      "electric wave&Hold %y\xA1%w = Charge attack&%c\xA5%w = First-person mode^%rWarning%w: Without magic, "
      "the&lightning will shock YOU.&Make sure you have enough magic!",
      "Du hast den %yLichtstab%w!&Eine magische Waffe mit der&Kraft des Blitzes.^%yBasisangriffe%w:&Hieb = 3 Blitze im "
      "Bogen&Stoß = 1 direkter Blitz&Sprung = Elektrische Entladung^%ySpezialangriffe%w:&Wirbelattacke = "
      "Elektrowelle&Halte %y\xA1%w = Aufladen&%c\xA5%w = Erste-Person^%rWarnung%w: Ohne Magie trifft&der Blitz DICH. "
      "Achte auf&genug Magie!",
      "Vous obtenez la %yBaguette de Lumière%w!&Une arme magique qui canalise&le pouvoir de la foudre.^%yAttaques de "
      "base%w:&Taille = 3 éclairs en éventail&Estoc = 1 éclair direct&Saut = Décharge électrique^%yAttaques "
      "spéciales%w:&Tourbillon = Vague électrique&Maintenez %y\xA1%w = Charge&%c\xA5%w = Première "
      "personne^%rAttention%w: Sans magie, la foudre&VOUS électrocutera. Assurez-vous&d'avoir assez de magie!" },

    // Device Items
    { RG_CANE_OF_SOMARIA, static_cast<ItemID>(ITEM_CANE_OF_SOMARIA),
      "You got the %rCane of Somaria%w!&A wand that creates magical&blocks out of thin air.^Press %y\xA1%w to swing and "
      "create&a %rmagical block%w. Up to %g3&blocks%w can exist at once.^The %roldest block%w is destroyed&when you "
      "create a 4th.^Use them to activate switches,&block enemies, or as&platforms to reach heights.",
      "Du hast den %rStab von Somaria%w!&Ein Stab der magische Blöcke&aus dem Nichts erschafft.^Drücke %y\xA1%w zum "
      "Schwingen&und erschaffe einen %rmagischen&Block%w. Bis zu %g3 Blöcke%w können&gleichzeitig existieren.^Der "
      "%rälteste Block%w wird zerstört&wenn du einen 4. erschaffst.^Nutze sie für Schalter, um Feinde&zu blockieren, "
      "oder als Plattform.",
      "Vous obtenez la %rCanne de Somaria%w!&Une baguette qui crée des&blocs magiques de nulle part.^Appuyez sur "
      "%y\xA1%w pour brandir&et créer un %rbloc magique%w.&Jusqu'à %g3 blocs%w peuvent exister.^Le %rbloc le plus "
      "ancien%w est&détruit quand vous en créez un 4e.^Utilisez-les pour activer des&interrupteurs, bloquer des "
      "ennemis,&ou comme plateformes." },

    { RG_DOMINION_ROD, static_cast<ItemID>(ITEM_DOMINION_ROD),
      "You got the %pDominion Rod%w!&An ancient artifact that can&possess and control enemies.^Press %y\xA1%w to fire a "
      "golden orb.&It can possess: %rBeamos%w,&%yArmos%w, and %cAnubis%w.^Once possessed, the enemy will&%gmimic your "
      "movements%w!&Walk to make it walk,&attack to make it attack.^Uses %gMagic%w while controlling.",
      "Du hast den %pKopierstab%w!&Ein uraltes Artefakt das Feinde&besitzen und kontrollieren kann.^Drücke %y\xA1%w um "
      "einen goldenen Orb&zu feuern. Er kann besitzen:&%rBeamos%w, %yArmos%w und %cAnubis%w.^Einmal besessen, wird der "
      "Feind&%gdeine Bewegungen imitieren%w!&Laufe um ihn laufen zu lassen,&greife an um ihn angreifen zu "
      "lassen.^Verbraucht %gMagie%w beim Kontrollieren.",
      "Vous obtenez la %pBaguette des Animes%w!&Un artefact ancien qui peut&posséder et contrôler les ennemis.^Appuyez "
      "sur %y\xA1%w pour tirer un&orbe doré. Il peut posséder:&%rBeamos%w, %yArmos%w et %cAnubis%w.^Une fois possédé, "
      "l'ennemi va&%gimiter vos mouvements%w!&Marchez pour le faire marcher,&attaquez pour le faire attaquer.^Utilise "
      "de la %gMagie%w pendant&le contrôle." },

    { RG_BEETLE, static_cast<ItemID>(ITEM_BEETLE),
      "You got the %gBeetle%w!&A remote-controlled mechanical&insect from ancient times.^%y\xA1%w = Launch "
      "beetle&%yAnalog Stick%w = Steer flight&%y\xA1%w again = Recall beetle&%y\xA0%w = Speed boost^The camera follows "
      "the beetle.&Use it to grab distant items,&hit switches, and scout ahead!",
      "Du hast den %gKäfer%w erhalten!&Ein ferngesteuertes mechanisches&Insekt aus alter Zeit.^%y\xA1%w = Käfer "
      "starten&%yAnalog-Stick%w = Flug steuern&%y\xA1%w erneut = Käfer zurückrufen&%y\xA0%w = Geschwindigkeitsschub^Die "
      "Kamera folgt dem Käfer.&Nutze ihn um Items zu holen,&Schalter zu treffen und voraus zu spähen!",
      "Vous obtenez le %gScarabée%w!&Un insecte mécanique télécommandé&des temps anciens.^%y\xA1%w = Lancer le "
      "scarabée&%yStick Analogique%w = Diriger le vol&%y\xA1%w à nouveau = Rappeler&%y\xA0%w = Accélération^La caméra "
      "suit le scarabée.&Utilisez-le pour attraper des objets,&activer des interrupteurs et explorer!" },

    { RG_DESIRE_SENSOR, static_cast<ItemID>(ITEM_DESIRE_SENSOR),
      "You got the %pDesire Sensor%w!&A cursed artifact that reveals&hidden treasures... at a cost.^Press %y\xA1%w to "
      "activate.&%rCosts 3 hearts%w per use!^%g(Randomizer only)%w:&%yGolden sparkles%w = Major items&remain in this "
      "area.&%rGanondorf laugh%w = Nothing left.",
      "Du hast den %pWunschdetektor%w!&Ein verfluchtes Artefakt das&verborgene Schätze enthüllt...&für einen "
      "Preis.^Drücke %y\xA1%w zum Aktivieren.&%rKostet 3 Herzen%w pro Nutzung!^%g(Nur im Randomizer)%w:&%yGoldene "
      "Funken%w = Wichtige Items&sind noch in diesem Gebiet.&%rGanondorfs Lachen%w = Nichts mehr da.",
      "Vous obtenez le %pDétecteur de Désir%w!&Un artefact maudit qui révèle&les trésors cachés... à un prix.^Appuyez "
      "sur %y\xA1%w pour activer.&%rCoûte 3 cœurs%w par utilisation!^%g(Randomizer uniquement)%w:&%yÉtincelles dorées%w "
      "= Objets majeurs&restent dans cette zone.&%rRire de Ganondorf%w = Plus rien." },

    // Placeholder items (pending implementation)
    { RG_PENDING_1, static_cast<ItemID>(ITEM_MINISH_CAP), "You got %pThe Minish Cap%w!&Fast travel between pod soils.",
      "Du hast %pThe Minish Cap%w!&Schnellreise zwischen Pod Soils.",
      "Vous obtenez %pPending Item 1%w!&Cet objet n'est pas encore implémenté." },

    { RG_LANTERN, static_cast<ItemID>(ITEM_LANTERN),
      "You got the %yLantern%w!&Catch fire from torches and&use it to light your way!",
      "Du hast die %yLaterne%w erhalten!&Fang Feuer von Fackeln und&nutze es um deinen Weg zu erleuchten!",
      "Vous obtenez la %yLanterne%w!&Capturez le feu des torches et&utilisez-le pour éclairer votre chemin!" },

    { RG_PENDING_3, static_cast<ItemID>(ITEM_POKEBALL),
      "You got the %yPoké Ball%w!&Use it to give orders to&a transformed Pikachu."
      "^%y\x9F%w combo  %y\xA0%w Thunder Jolt&Stick+%y\x9F%w/%y\xA0%w: smash / special&%y\xA2%w crouch  %y\xA3%w bubble shield&%y\xA1%w-buttons: special items",
      "Du hast den %yPokéball%w erhalten!&Damit gibst du einem&verwandelten Pikachu Befehle."
      "^%y\x9F%w Combo  %y\xA0%w Donner-Schock&Stick+%y\x9F%w/%y\xA0%w: Smash / Special&%y\xA2%w Hocken  %y\xA3%w Blasen-Schild&%y\xA1%w-Tasten: Special-Items",
      "Vous obtenez la %yPoké Ball%w!&Donnez des ordres à un&Pikachu transformé."
      "^%y\x9F%w combo  %y\xA0%w Tonnerre&Stick+%y\x9F%w/%y\xA0%w: smash / spécial&%y\xA2%w accroupi  %y\xA3%w bouclier&%y\xA1%w: objets spéciaux" },

    // ─────────────────────────────────────────────────────────────────────────
    // Extended Equipment (12 items, equipment page 2 - toggled via [L] in pause)
    // ─────────────────────────────────────────────────────────────────────────
    { RG_EXT_CANE_OF_BYRNA, static_cast<ItemID>(ITEM_EXT_SWORD_1),
      "You got the %cCane of Byrna%w!&A blue cane of legend.^Equip on the %ysword slot%w&(%y\xA2%w toggles equipment pages).^Wields like the %cBiggoron Sword%w&(long range, two-handed). %gSpin%w&and %gcharge attacks%w always work.^Every melee hit %crestores HP%w&and %crefills Magic%w!",
      "Du hast den %cStab von Byrna%w!&Ein blauer Stab der Legenden.^Rüste ihn am %ySchwert-Platz%w aus&(%y\xA2%w wechselt Seiten).^Führt sich wie das %cBiggoron-Schwert%w&(lange Reichweite, beidhändig). %gKreisangriffe%w&und %gAufladeangriffe%w gehen immer.^Jeder Treffer %cstellt HP%w und&%cMagie%w wieder her!",
      "Vous obtenez la %cCanne de Byrna%w!&Une canne bleue de légende.^Équipez-la dans l'%yemplacement épée%w&(%y\xA2%w change de page).^Se manie comme l'%cÉpée de Biggoron%w&(longue portée, à deux mains).&%gAttaques tournoyantes%w et %gchargées%w&fonctionnent toujours.^Chaque coup %crestaure des PV%w&et %crecharge la Magie%w!" },

    { RG_EXT_FOUR_SWORD, static_cast<ItemID>(ITEM_EXT_SWORD_2),
      "You got the %gFour Sword%w!&A blade that splits its wielder&into four heroes.^Equip on the %ysword slot%w (%y\xA2%w toggles).^Hold %y\xA3%w + %y\xA0%w for 15 frames ->&%g3 colored clones%w (Red/Blue/Purple)&spawn around you in a triangle.^Each clone costs %g12 Magic%w.&Clones %gmirror your swings%w and copy&your %garrows%w, %gbombs%w and %gboomerang%w.^Enemy hits kill them.",
      "Du hast das %gVier-Schwert%w!&Eine Klinge die ihren Träger&in vier Helden teilt.^Rüste es am %ySchwert-Platz%w aus.^Halte %y\xA3%w + %y\xA0%w 15 Frames ->&%g3 farbige Klone%w (Rot/Blau/Violett)&erscheinen im Dreieck.^Jeder Klon kostet %g12 Magie%w.&Klone %gspiegeln deine Schwerthiebe%w und&kopieren %gPfeile%w, %gBomben%w und %gBumerang%w.^Feindtreffer töten sie.",
      "Vous obtenez l'%gÉpée de Quatre%w!&Une lame qui divise son porteur&en quatre héros.^Équipez-la dans l'%yemplacement épée%w.^Maintenez %y\xA3%w + %y\xA0%w 15 frames ->&%g3 clones colorés%w (Rouge/Bleu/Violet)&apparaissent en triangle.^Chaque clone coûte %g12 Magie%w.&Les clones %gimitent vos coups%w et copient&%gflèches%w, %gbombes%w et %gboomerang%w.^Les ennemis les tuent au contact." },

    { RG_EXT_IRON_KNUCKLE_AXE, static_cast<ItemID>(ITEM_EXT_SWORD_3),
      "You got the %rIron Knuckle's Axe%w!&The massive tomahawk of Ganon's&armored knights.^Equip on the %ysword slot%w (%y\xA2%w toggles).^Wields like the %rMegaton Hammer%w&with chunky heavy swings:&%gdouble damage%w, %gdouble reach%w,&slower walk while held.^Hold %y\xA3%w + %y\xA0%w 15 frames to %rthrow%w&the axe - flies forward, then&boomerangs back to your hand.",
      "Du hast die %rEisenknöchel-Axt%w!&Der massive Tomahawk der&gepanzerten Ritter Ganons.^Rüste sie am %ySchwert-Platz%w aus.^Führt sich wie der %rStahlhammer%w&mit schwerem chunky Schwung:&%gdoppelter Schaden%w, %gdoppelte Reichweite%w,&langsameres Gehen.^Halte %y\xA3%w + %y\xA0%w 15 Frames um die&Axt zu %rwerfen%w - fliegt nach vorn,&kommt dann zu dir zurück.",
      "Vous obtenez la %rHache d'Iron Knuckle%w!&Le tomahawk massif des chevaliers&en armure de Ganon.^Équipez-la dans l'%yemplacement épée%w.^Se manie comme la %rMasse des Titans%w&avec des coups lourds:&%gdouble dégâts%w, %gdouble portée%w,&marche plus lente.^Maintenez %y\xA3%w + %y\xA0%w 15 frames pour&%rlancer%w la hache - elle vole&et revient en boomerang." },

    { RG_EXT_DIVINE_SHIELD, static_cast<ItemID>(ITEM_EXT_SHIELD_1),
      "You got the %yDivine Shield%w!&A blessed wooden shield said to&repel even the wrath of fire.^Equip on the %yshield slot%w (%y\xA2%w toggles).^Light wooden shield BUT %rfireproof%w -&fire breath, Dodongo flames and&torches will not burn it.^%cPerfect Parry%w (%y\xA3%w + block within&10 frames of an attack):&%cfreezes ALL enemies%w on screen!",
      "Du hast den %yGötterschild%w!&Ein gesegneter Holzschild der selbst&dem Zorn des Feuers widersteht.^Rüste ihn am %ySchild-Platz%w aus.^Leichter Holzschild ABER %rfeuerfest%w -&Feueratem, Dodongo-Flammen und&Fackeln verbrennen ihn nicht.^%cPerfekte Parade%w (%y\xA3%w + block in&den ersten 10 Frames eines Angriffs):&%cfriert ALLE Feinde%w auf dem Schirm ein!",
      "Vous obtenez le %yBouclier Divin%w!&Un bouclier en bois béni qui&résiste à la colère du feu.^Équipez-le dans l'%yemplacement bouclier%w.^Bouclier en bois MAIS %rignifuge%w -&souffle de feu, flammes de Dodongo&et torches ne le brûlent pas.^%cParade Parfaite%w (%y\xA3%w + bloquer dans&les 10 premières frames d'une attaque):&%cgèle TOUS les ennemis%w à l'écran!" },

    { RG_EXT_SHEIKAH_SHIELD, static_cast<ItemID>(ITEM_EXT_SHIELD_2),
      "You got the %cSheikah Shield%w!&A ceremonial shield bearing the&eye of the Sheikah tribe.^Equip on the %yshield slot%w (%y\xA2%w toggles).^Hold %y\xA3%w to block normally.&Currently a %ycosmetic shield%w -&no special effect.",
      "Du hast den %cSheikah-Schild%w!&Ein zeremonieller Schild mit dem&Auge des Sheikah-Stammes.^Rüste ihn am %ySchild-Platz%w aus.^%y\xA3%w zum normalen Blocken.&Derzeit ein %ykosmetischer Schild%w -&kein besonderer Effekt.",
      "Vous obtenez le %cBouclier Sheikah%w!&Un bouclier cérémoniel portant&l'œil de la tribu Sheikah.^Équipez-le dans l'%yemplacement bouclier%w.^Maintenez %y\xA3%w pour parer normalement.&Actuellement un %ybouclier cosmétique%w -&pas d'effet particulier." },

    { RG_EXT_SHIELD_OF_IKANA, static_cast<ItemID>(ITEM_EXT_SHIELD_3),
      "You got the %pShield of Ikana%w!&A cursed mirror shield from the&fallen kingdom of Ikana.^Equip on the %yshield slot%w (%y\xA2%w toggles).^%cSoul Drain%w (%y\xA3%w + block within&12 frames of an attack):&drains the attacker's %rHP%w and&heals you for half a heart.^%pDeath Save%w: when struck dead,&%previves once per scene%w with&3 hearts and a dark aura.",
      "Du hast den %pSchild von Ikana%w!&Ein verfluchter Spiegelschild aus&dem gefallenen Reich Ikana.^Rüste ihn am %ySchild-Platz%w aus.^%cSeelenraub%w (%y\xA3%w + block in&den ersten 12 Frames eines Angriffs):&saugt %rHP%w des Angreifers und&heilt dich um ein halbes Herz.^%pTodesrettung%w: bei tödlichem Treffer&%pwiederbelebt einmal pro Szene%w mit&3 Herzen und dunkler Aura.",
      "Vous obtenez le %pBouclier d'Ikana%w!&Un bouclier-miroir maudit du&royaume déchu d'Ikana.^Équipez-le dans l'%yemplacement bouclier%w.^%cVol d'Âme%w (%y\xA3%w + bloquer dans&les 12 premières frames d'une attaque):&vole les %rPV%w de l'attaquant et&vous soigne d'un demi-cœur.^%pSauvegarde de Mort%w: ressuscite&%pune fois par scène%w avec 3 cœurs&et une aura sombre." },

    { RG_EXT_MAGIC_CAPE, static_cast<ItemID>(ITEM_EXT_TUNIC_1),
      "You got the %pMagic Cape%w!&Ganondorf's enchanted cloak,&woven of pure dark mantle cloth.^Equip on the %ytunic slot%w (%y\xA2%w toggles).^Real %pcloth physics%w - the cape&drapes from your shoulders and&sways with movement and wind.^You %crecover half the Magic%w&you spend each frame&(rounded up).",
      "Du hast den %pZauberumhang%w!&Ganondorfs verzauberter Mantel,&gewebt aus dunklem Mantelstoff.^Rüste ihn am %yTunika-Platz%w aus.^Echte %pStoff-Physik%w - der Umhang&fällt von deinen Schultern und&schwingt mit Bewegung und Wind.^Du %cerhältst die halbe Magie%w&zurück die du verbrauchst&(aufgerundet).",
      "Vous obtenez la %pCape Magique%w!&Le manteau enchanté de Ganondorf,&tissé de pure étoffe sombre.^Équipez-la dans l'%yemplacement tunique%w.^%pPhysique de tissu%w réelle - la cape&pend de vos épaules et ondule&avec le mouvement et le vent.^Vous %crécupérez la moitié de la Magie%w&dépensée chaque frame (arrondi&au supérieur)." },

    { RG_EXT_SPIRIT_BREASTPLATE, static_cast<ItemID>(ITEM_EXT_TUNIC_2),
      "You got the %ySpirit Breastplate%w!&The golden armor of the Iron&Knuckle Nabooru.^Equip on the %ytunic slot%w (%y\xA2%w toggles).^Damage costs %gRupees%w instead&of hearts (1 HP = 1 Rupee).&%gPassive drain%w: 1 Rupee every&30 frames while equipped.^If your wallet runs %rempty%w,&you take damage normally and&move at half speed.",
      "Du hast den %ySpirit-Brustpanzer%w!&Die goldene Rüstung der Eisenknöchel&Nabooru.^Rüste ihn am %yTunika-Platz%w aus.^Schaden kostet %gRupien%w statt&Herzen (1 HP = 1 Rupie).&%gPassiver Verbrauch%w: 1 Rupie alle&30 Frames im Tragen.^Wenn dein Beutel %rleer%w ist,&erleidest du Schaden normal und&bewegst dich halb so schnell.",
      "Vous obtenez le %yPlastron Spirituel%w!&L'armure dorée de l'Iron Knuckle&Nabooru.^Équipez-le dans l'%yemplacement tunique%w.^Les dégâts coûtent des %gRubis%w au&lieu de cœurs (1 PV = 1 Rubis).&%gDrain passif%w: 1 Rubis toutes&les 30 frames tant que porté.^Si votre bourse est %rvide%w,&vous prenez les dégâts normalement&et bougez à mi-vitesse." },

    { RG_EXT_CHAMPIONS_TUNIC, static_cast<ItemID>(ITEM_EXT_TUNIC_3),
      "You got the %cChampion's Tunic%w!&The blue garb of Hyrule's chosen,&blessed with battle aura.^Equip on the %ytunic slot%w (%y\xA2%w toggles).^Adult Link gets the full %cBOTW Link%w&model. All ages get the combat:^%gFlurry Rush%w: sidehop or backflip&near an enemy -> world slows to&15% with iframes for ~2s. Land&up to 7 hits, then bonus damage.^%cBullet Time%w: %y\xA5%w-target while airborne&with bow/slingshot/hookshot/boomerang&-> time slows, you float, stick aims&the shot.",
      "Du hast die %cRüstung des Helden%w!&Die blaue Tracht des Auserwählten&Hyrules, mit Kampfaura gesegnet.^Rüste sie am %yTunika-Platz%w aus.^Adult Link erhält das volle %cBOTW-Link%w&Modell. Beide Altersstufen kämpfen damit:^%gFlurry Rush%w: Seitsprung oder Backflip&neben einem Feind -> Welt verlangsamt&auf 15% mit i-Frames für ~2s.&Lande bis zu 7 Treffer.^%cBullet Time%w: %y\xA5%w-fokussieren in der Luft&mit Bogen/Schleuder/Greifhaken/Bumerang&-> Zeit verlangsamt, du schwebst,&Stick zielt den Schuss.",
      "Vous obtenez la %cTunique du Héros%w!&Le vêtement bleu de l'élu d'Hyrule,&béni d'une aura de combat.^Équipez-la dans l'%yemplacement tunique%w.^Link adulte obtient le modèle complet&%cLink BOTW%w. Les deux âges combattent:^%gFlurry Rush%w: esquive ou saut arrière&près d'un ennemi -> le monde ralentit&à 15% avec i-frames pendant ~2s.&Jusqu'à 7 coups.^%cBullet Time%w: %y\xA5%w-cible en l'air avec&arc/lance-pierre/grappin/boomerang ->&le temps ralentit, vous flottez,&le stick vise le tir." },

    { RG_EXT_PEGASUS_ANKLET, static_cast<ItemID>(ITEM_EXT_BOOTS_1),
      "You got the %rPegasus Anklet%w!&Winged anklets that grant the&speed of the legendary Pegasus.^Equip on the %yboots slot%w (%y\xA2%w toggles).^Hold %y\xA0%w after a sword swing&(intercepts the spin attack charge):&Link %glunges forward%w with sword&extended, dealing damage on contact.^A %gwind cone barrier%w forms in&front while you have Magic&(1 MP per 15 frames).&Walls cause a %rbonk%w recovery.",
      "Du hast den %rPegasus-Fußreif%w!&Geflügelte Fußreifen mit der&Geschwindigkeit des Pegasus.^Rüste sie am %yStiefel-Platz%w aus.^Halte %y\xA0%w nach einem Schwertschlag&(unterbricht den Aufladeangriff):&Link %gstürmt vor%w mit ausgestrecktem&Schwert, Schaden bei Kontakt.^Ein %gWindkegel%w bildet sich vor dir&solange du Magie hast (1 MP pro&15 Frames). Wände lösen einen&%rZusammenstoß%w aus.",
      "Vous obtenez le %rBracelet de Pégase%w!&Des bracelets ailés qui octroient&la vitesse du légendaire Pégase.^Équipez-le dans l'%yemplacement bottes%w.^Maintenez %y\xA0%w après un coup d'épée&(intercepte la charge tournoyante):&Link %ss'élance%w l'épée tendue,&infligeant des dégâts au contact.^Un %gcône de vent%w protecteur se forme&devant tant que vous avez de la Magie&(1 MP toutes les 15 frames).&Les murs causent un %rchoc%w." },

    { RG_EXT_PENDANT_OF_MEMORIES, static_cast<ItemID>(ITEM_EXT_BOOTS_2),
      "You got the %pPendant of Memories%w!&A pendant carrying the techniques&of heroes past.^Equip on the %yboots slot%w (%y\xA2%w toggles).^Three combat techniques unlock:^%c#1 Mortal Draw%w (TP): %y\xA0%w near an&enemy + sheathed + still + NOT&%y\xA5%w-targeting -> devastating draw&slash, often a one-hit kill.^%c#2 Ground Pound%w (Smash): %y\xA0%w in&air with sword -> fast fall ->&pogo bounce on hit, shockwave on landing.^%c#3 Parry Leap%w (WW): %y\xA5%w-target +&3 sidehops + %y\xA0%w -> parabolic arc&over the foe, land behind them.",
      "Du hast das %pAmulett der Erinnerungen%w!&Ein Anhänger mit Techniken vergangener&Helden.^Rüste es am %yStiefel-Platz%w aus.^Drei Kampftechniken werden frei:^%c#1 Mortal Draw%w (TP): %y\xA0%w bei einem&Feind + eingesteckt + still + NICHT&%y\xA5%w-fokussieren -> vernichtender Hieb,&oft One-Hit-Kill.^%c#2 Ground Pound%w (Smash): %y\xA0%w in&der Luft mit Schwert -> schneller Fall&-> Bounce bei Treffer, Schockwelle beim&Landen.^%c#3 Parry Leap%w (WW): %y\xA5%w-fokussieren&+ 3 Seitsprünge + %y\xA0%w -> parabolischer&Bogen über den Feind, hinter ihm landen.",
      "Vous obtenez le %pPendentif des Souvenirs%w!&Un pendentif portant les techniques&des héros passés.^Équipez-le dans l'%yemplacement bottes%w.^Trois techniques de combat:^%c#1 Mortal Draw%w (TP): %y\xA0%w près d'un&ennemi + rengainé + immobile + PAS&en %y\xA5%w-cible -> tranche dévastatrice,&souvent un one-shot.^%c#2 Ground Pound%w (Smash): %y\xA0%w en l'air&avec épée -> chute rapide -> rebond&sur impact, onde de choc à l'atterrissage.^%c#3 Parry Leap%w (WW): %y\xA5%w-cible +&3 esquives + %y\xA0%w -> arc parabolique&par-dessus l'ennemi, atterrir derrière." },

    { RG_EXT_WATER_DRAGON_SCALE, static_cast<ItemID>(ITEM_EXT_BOOTS_3),
      "You got the %bWater Dragon Scale%w!&A blessed scale of the Water Dragon,&master of the depths.^Equip on the %yboots slot%w (%y\xA2%w toggles).^Adult Link only - no effect&on Young Link.^Activates real %bZora swim mechanics%w&1:1 from MM: surface walk,&%bfast dolphin swim%w, %bswim dash%w (%y\xA0%w),&%bdolphin jump%w arcs out of water.^%cIron Boots%w let you sink while&wearing the Scale.",
      "Du hast die %bWasserdrachen-Schuppe%w!&Eine gesegnete Schuppe des&Wasserdrachen, Herrscher der Tiefen.^Rüste sie am %yStiefel-Platz%w aus.^Nur erwachsener Link - bei jungem&Link kein Effekt.^Aktiviert echte %bZora-Schwimmmechanik%w&1:1 aus MM: Wasserlauf,&%bschneller Delfinschwimmen%w, %bSchwimm-Dash%w&(%y\xA0%w), %bDelfinsprung%w aus dem Wasser.^%cEisenstiefel%w lassen dich sinken&während du die Schuppe trägst.",
      "Vous obtenez l'%bÉcaille du Dragon d'Eau%w!&Une écaille bénie du Dragon d'Eau,&maître des profondeurs.^Équipez-la dans l'%yemplacement bottes%w.^Link adulte uniquement - aucun&effet sur Jeune Link.^Active les vraies %bmécaniques Zora%w&1:1 de MM: marche en surface,&%bnage dauphin rapide%w, %bdash de nage%w&(%y\xA0%w), %bsaut de dauphin%w hors de l'eau.^%cBottes de Plomb%w pour couler&en portant l'écaille." },

    { RG_BOTTLE_WITH_MAGIC_MUSHROOM, static_cast<ItemID>(ITEM_BOTTLE_WITH_MAGIC_MUSHROOM),
      "You got a %gBottle with Magic Mushroom%w!&A fragrant Termina mushroom plucked&by the keen nose of the Mask of Scents.^Stored in an empty bottle.&Drop it later for unknown effects -&or simply admire the catch.",
      "Du hast eine %gFlasche mit Zauberpilz%w!&Ein duftender Termina-Pilz, geschnüffelt&von der Geruchsmaske.^In einer leeren Flasche aufbewahrt.&Lass ihn später fallen für unbekannte&Effekte - oder bewundere ihn.",
      "Vous obtenez une %gFiole avec Champignon Magique%w!&Un champignon parfumé de Termina,&flairé par le Masque des Odeurs.^Stocké dans une fiole vide.&À déposer plus tard pour des effets&inconnus - ou à contempler." },
};
static constexpr size_t customItemMessageCount = sizeof(customItemMessages) / sizeof(customItemMessages[0]);

// Helper function to get custom item message by RG ID
const CustomItemMessageEntry* GetCustomItemMessage(s16 rgId) {
    for (size_t i = 0; i < customItemMessageCount; i++) {
        if (customItemMessages[i].rgId == rgId) {
            return &customItemMessages[i];
        }
    }
    return nullptr;
}

bool Rando_HandleSpoilerDrop(char* filePath) {
    if (SohUtils::IsStringEmpty(filePath)) {
        return false;
    }

    try {
        std::ifstream stream(filePath);
        if (!stream) {
            return false;
        }

        nlohmann::json json;
        stream >> json;

        if (json.contains("version") && json.contains("finalSeed")) {
            CVarSetString(CVAR_GENERAL("RandomizerDroppedFile"), filePath);
            CVarSetInteger(CVAR_GENERAL("RandomizerNewFileDropped"), 1);
            return true;
        }
    } catch ([[maybe_unused]] std::exception& e) {}
    return false;
}

Randomizer::Randomizer() {
    Rando::StaticData::InitItemTable();
    Rando::StaticData::InitLocationTable();

    for (auto area : rcAreaNames) {
        SpoilerfileAreaNameToEnum[area.second] = area.first;
    }
    SpoilerfileAreaNameToEnum["Inside Ganon's Castle"] = RCAREA_GANONS_CASTLE;
    SpoilerfileAreaNameToEnum["the Lost Woods"] = RCAREA_LOST_WOODS;
    SpoilerfileAreaNameToEnum["the Market"] = RCAREA_MARKET;
    SpoilerfileAreaNameToEnum["the Graveyard"] = RCAREA_GRAVEYARD;
    SpoilerfileAreaNameToEnum["Haunted Wasteland"] = RCAREA_WASTELAND;
    SpoilerfileAreaNameToEnum["outside Ganon's Castle"] = RCAREA_HYRULE_CASTLE;
    for (size_t c = 0; c < Rando::StaticData::hintTypeNames.size(); c++) {
        SpoilerfileHintTypeNameToEnum[Rando::StaticData::hintTypeNames[(HintType)c].GetEnglish(MF_CLEAN)] = (HintType)c;
    }

    Ship::Context::GetRawInstance()->GetFileDropMgr()->RegisterDropHandler(Rando_HandleSpoilerDrop);
}

Randomizer::~Randomizer() {
}

std::unordered_map<std::string, SceneID> spoilerFileDungeonToScene = {
    { "Deku Tree", SCENE_DEKU_TREE },
    { "Dodongo's Cavern", SCENE_DODONGOS_CAVERN },
    { "Jabu Jabu's Belly", SCENE_JABU_JABU },
    { "Forest Temple", SCENE_FOREST_TEMPLE },
    { "Fire Temple", SCENE_FIRE_TEMPLE },
    { "Water Temple", SCENE_WATER_TEMPLE },
    { "Spirit Temple", SCENE_SPIRIT_TEMPLE },
    { "Shadow Temple", SCENE_SHADOW_TEMPLE },
    { "Bottom of the Well", SCENE_BOTTOM_OF_THE_WELL },
    { "Ice Cavern", SCENE_ICE_CAVERN },
    { "Gerudo Training Ground", SCENE_GERUDO_TRAINING_GROUND },
    { "Ganon's Castle", SCENE_INSIDE_GANONS_CASTLE }
};

// used for items that only set a rand inf when obtained
std::unordered_map<RandomizerGet, RandomizerInf> randomizerGetToRandInf = {
    { RG_FISHING_POLE, RAND_INF_FISHING_POLE_FOUND },
    { RG_BRONZE_SCALE, RAND_INF_CAN_SWIM },
    { RG_POWER_BRACELET, RAND_INF_CAN_GRAB },
    { RG_CLIMB, RAND_INF_CAN_CLIMB },
    { RG_CRAWL, RAND_INF_CAN_CRAWL },
    { RG_OPEN_CHEST, RAND_INF_CAN_OPEN_CHEST },
    { RG_SPEAK_DEKU, RAND_INF_CAN_SPEAK_DEKU },
    { RG_SPEAK_GERUDO, RAND_INF_CAN_SPEAK_GERUDO },
    { RG_SPEAK_GORON, RAND_INF_CAN_SPEAK_GORON },
    { RG_SPEAK_HYLIAN, RAND_INF_CAN_SPEAK_HYLIAN },
    { RG_SPEAK_KOKIRI, RAND_INF_CAN_SPEAK_KOKIRI },
    { RG_SPEAK_ZORA, RAND_INF_CAN_SPEAK_ZORA },
    { RG_QUIVER_INF, RAND_INF_HAS_INFINITE_QUIVER },
    { RG_BOMB_BAG_INF, RAND_INF_HAS_INFINITE_BOMB_BAG },
    { RG_BULLET_BAG_INF, RAND_INF_HAS_INFINITE_BULLET_BAG },
    { RG_STICK_UPGRADE_INF, RAND_INF_HAS_INFINITE_STICK_UPGRADE },
    { RG_NUT_UPGRADE_INF, RAND_INF_HAS_INFINITE_NUT_UPGRADE },
    { RG_MAGIC_INF, RAND_INF_HAS_INFINITE_MAGIC_METER },
    { RG_BOMBCHU_INF, RAND_INF_HAS_INFINITE_BOMBCHUS },
    { RG_WALLET_INF, RAND_INF_HAS_INFINITE_MONEY },
    { RG_OCARINA_A_BUTTON, RAND_INF_HAS_OCARINA_A },
    { RG_OCARINA_C_UP_BUTTON, RAND_INF_HAS_OCARINA_C_UP },
    { RG_OCARINA_C_DOWN_BUTTON, RAND_INF_HAS_OCARINA_C_DOWN },
    { RG_OCARINA_C_LEFT_BUTTON, RAND_INF_HAS_OCARINA_C_LEFT },
    { RG_OCARINA_C_RIGHT_BUTTON, RAND_INF_HAS_OCARINA_C_RIGHT },
    { RG_DEATH_MOUNTAIN_CRATER_BEAN_SOUL, RAND_INF_DEATH_MOUNTAIN_CRATER_BEAN_SOUL },
    { RG_DEATH_MOUNTAIN_TRAIL_BEAN_SOUL, RAND_INF_DEATH_MOUNTAIN_TRAIL_BEAN_SOUL },
    { RG_DESERT_COLOSSUS_BEAN_SOUL, RAND_INF_DESERT_COLOSSUS_BEAN_SOUL },
    { RG_GERUDO_VALLEY_BEAN_SOUL, RAND_INF_GERUDO_VALLEY_BEAN_SOUL },
    { RG_GRAVEYARD_BEAN_SOUL, RAND_INF_GRAVEYARD_BEAN_SOUL },
    { RG_KOKIRI_FOREST_BEAN_SOUL, RAND_INF_KOKIRI_FOREST_BEAN_SOUL },
    { RG_LAKE_HYLIA_BEAN_SOUL, RAND_INF_LAKE_HYLIA_BEAN_SOUL },
    { RG_LOST_WOODS_BRIDGE_BEAN_SOUL, RAND_INF_LOST_WOODS_BRIDGE_BEAN_SOUL },
    { RG_LOST_WOODS_BEAN_SOUL, RAND_INF_LOST_WOODS_BEAN_SOUL },
    { RG_ZORAS_RIVER_BEAN_SOUL, RAND_INF_ZORAS_RIVER_BEAN_SOUL },
    { RG_GOHMA_SOUL, RAND_INF_GOHMA_SOUL },
    { RG_KING_DODONGO_SOUL, RAND_INF_KING_DODONGO_SOUL },
    { RG_BARINADE_SOUL, RAND_INF_BARINADE_SOUL },
    { RG_PHANTOM_GANON_SOUL, RAND_INF_PHANTOM_GANON_SOUL },
    { RG_VOLVAGIA_SOUL, RAND_INF_VOLVAGIA_SOUL },
    { RG_MORPHA_SOUL, RAND_INF_MORPHA_SOUL },
    { RG_BONGO_BONGO_SOUL, RAND_INF_BONGO_BONGO_SOUL },
    { RG_TWINROVA_SOUL, RAND_INF_TWINROVA_SOUL },
    { RG_GANON_SOUL, RAND_INF_GANON_SOUL },
};

#ifdef _MSC_VER
#pragma optimize("", off)
#else
#pragma GCC push_options
#pragma GCC optimize("O0")
#endif
bool Randomizer::SpoilerFileExists(const char* spoilerFileName) {
    static std::unordered_map<std::string, bool> existsCache;
    static std::unordered_map<std::string, std::filesystem::file_time_type> lastModifiedCache;

    if (strcmp(spoilerFileName, "") == 0) {
        return false;
    }

    std::string sanitizedFileName = SohUtils::Sanitize(spoilerFileName);

    try {
        // Check if file exists and get last modified time
        std::filesystem::path filePath(sanitizedFileName);
        if (!std::filesystem::exists(filePath)) {
            // Cache and return false if file doesn't exist
            existsCache[sanitizedFileName] = false;
            lastModifiedCache.erase(sanitizedFileName);
            return false;
        }

        auto currentLastModified = std::filesystem::last_write_time(filePath);

        // Check cache first
        auto existsCacheIt = existsCache.find(sanitizedFileName);
        auto lastModifiedCacheIt = lastModifiedCache.find(sanitizedFileName);

        // If we have a valid cache entry and the file hasn't been modified
        if (existsCacheIt != existsCache.end() && lastModifiedCacheIt != lastModifiedCache.end() &&
            lastModifiedCacheIt->second == currentLastModified) {
            return existsCacheIt->second;
        }

        // Cache miss or file modified - need to check contents
        std::ifstream spoilerFileStream(sanitizedFileName);
        if (spoilerFileStream) {
            nlohmann::json contents;
            spoilerFileStream >> contents;
            spoilerFileStream.close();

            bool isValid = contents.contains("version") &&
                           strcmp(std::string(contents["version"]).c_str(), (char*)gBuildVersion) == 0;

            if (!isValid) {
                SohGui::RegisterPopup(
                    "Old Spoiler Version",
                    "The spoiler file located at\n" + std::string(spoilerFileName) +
                        "\nwas made by a version that doesn't match the currently running version.\n" +
                        "Loading for this file has been cancelled.");
                CVarClear(CVAR_GENERAL("SpoilerLog"));
                Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
            }

            // Update cache
            existsCache[sanitizedFileName] = isValid;
            lastModifiedCache[sanitizedFileName] = currentLastModified;
            return isValid;
        }

        // File couldn't be opened
        existsCache[sanitizedFileName] = false;
        lastModifiedCache.erase(sanitizedFileName);
        return false;

    } catch (const std::filesystem::filesystem_error&) {
        // Handle filesystem errors by invalidating cache
        existsCache[sanitizedFileName] = false;
        lastModifiedCache.erase(sanitizedFileName);
        return false;
    }
}
#ifdef _MSC_VER
#pragma optimize("", on)
#else
#pragma GCC pop_options
#endif

// Reference soh/src/overlays/actors/ovl_En_GirlA/z_en_girla.h
std::unordered_map<RandomizerGet, EnGirlAShopItem> randomizerGetToEnGirlShopItem = {
    { RG_BUY_DEKU_NUTS_5, SI_DEKU_NUTS_5 },
    { RG_BUY_ARROWS_30, SI_ARROWS_30 },
    { RG_BUY_ARROWS_50, SI_ARROWS_50 },
    { RG_BUY_BOMBS_525, SI_BOMBS_5_R25 },
    { RG_BUY_DEKU_NUTS_10, SI_DEKU_NUTS_10 },
    { RG_BUY_DEKU_STICK_1, SI_DEKU_STICK },
    { RG_BUY_BOMBS_10, SI_BOMBS_10 },
    { RG_BUY_FISH, SI_FISH },
    { RG_BUY_RED_POTION_30, SI_RED_POTION_R30 },
    { RG_BUY_GREEN_POTION, SI_GREEN_POTION },
    { RG_BUY_BLUE_POTION, SI_BLUE_POTION },
    { RG_BUY_HYLIAN_SHIELD, SI_HYLIAN_SHIELD },
    { RG_BUY_DEKU_SHIELD, SI_DEKU_SHIELD },
    { RG_BUY_GORON_TUNIC, SI_GORON_TUNIC },
    { RG_BUY_ZORA_TUNIC, SI_ZORA_TUNIC },
    { RG_BUY_HEART, SI_RECOVERY_HEART },
    { RG_BUY_BOMBCHUS_10, SI_BOMBCHU_10_1 },
    { RG_BUY_BOMBCHUS_20, SI_BOMBCHU_20_1 },
    { RG_BUY_DEKU_SEEDS_30, SI_DEKU_SEEDS_30 },
    { RG_BUY_BLUE_FIRE, SI_BLUE_FIRE },
    { RG_BUY_BOTTLE_BUG, SI_BUGS },
    { RG_BUY_POE, SI_POE },
    { RG_BUY_FAIRYS_SPIRIT, SI_FAIRY },
    { RG_BUY_ARROWS_10, SI_ARROWS_10 },
    { RG_BUY_BOMBS_20, SI_BOMBS_20 },
    { RG_BUY_BOMBS_30, SI_BOMBS_30 },
    { RG_BUY_BOMBS_535, SI_BOMBS_5_R35 },
    { RG_BUY_RED_POTION_40, SI_RED_POTION_R40 },
    { RG_BUY_RED_POTION_50, SI_RED_POTION_R50 },
};

std::map<s32, TrialKey> trialFlagToTrialKey = {
    { EVENTCHKINF_COMPLETED_LIGHT_TRIAL, TK_LIGHT_TRIAL },   { EVENTCHKINF_COMPLETED_FOREST_TRIAL, TK_FOREST_TRIAL },
    { EVENTCHKINF_COMPLETED_FIRE_TRIAL, TK_FIRE_TRIAL },     { EVENTCHKINF_COMPLETED_WATER_TRIAL, TK_WATER_TRIAL },
    { EVENTCHKINF_COMPLETED_SPIRIT_TRIAL, TK_SPIRIT_TRIAL }, { EVENTCHKINF_COMPLETED_SHADOW_TRIAL, TK_SHADOW_TRIAL },
};

bool Randomizer::IsTrialRequired(s32 trialFlag) {
    return Rando::Context::GetInstance()->GetTrial(trialFlagToTrialKey[trialFlag])->IsRequired();
}

GetItemEntry Randomizer::GetItemFromActor(s16 actorId, s16 sceneNum, s16 actorParams, GetItemID ogItemId,
                                          bool checkObtainability) {
    return Rando::Context::GetInstance()->GetFinalGIEntry(GetCheckFromActor(actorId, sceneNum, actorParams),
                                                          checkObtainability, ogItemId);
}

ItemObtainability Randomizer::GetItemObtainabilityFromRandomizerCheck(RandomizerCheck randomizerCheck) {
    return GetItemObtainabilityFromRandomizerGet(
        Rando::Context::GetInstance()->GetItemLocation(randomizerCheck)->GetPlacedRandomizerGet());
}

ItemObtainability Randomizer::GetItemObtainabilityFromRandomizerGet(RandomizerGet randoGet) {
    if (randomizerGetToRandInf.find(randoGet) != randomizerGetToRandInf.end()) {
        return Flags_GetRandomizerInf(randomizerGetToRandInf.find(randoGet)->second) ? CANT_OBTAIN_ALREADY_HAVE
                                                                                     : CAN_OBTAIN;
    }

    // This is needed since Plentiful item pool also adds a third progressive wallet
    // but we should not get Tycoon's Wallet from it if it is off.
    bool tycoonWallet = GetRandoSettingValue(RSK_INCLUDE_TYCOON_WALLET);

    // Same thing with the infinite upgrades, if we're not shuffling them
    // and we're using the Plentiful item pool, we should prevent the infinite
    // upgrades from being gotten
    u8 infiniteUpgrades = GetRandoSettingValue(RSK_INFINITE_UPGRADES);

    u8 numWallets = 2 + (u8)tycoonWallet + (infiniteUpgrades != RO_INF_UPGRADES_OFF ? 1 : 0);
    switch (randoGet) {
        case RG_NONE:
        case RG_TRIFORCE:
        case RG_HINT:
        case RG_MAX:
        case RG_SOLD_OUT:
            return CANT_OBTAIN_MISC;

        // Equipment
        case RG_KOKIRI_SWORD:
            return !CHECK_OWNED_EQUIP(EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_KOKIRI) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_MASTER_SWORD:
            return !CHECK_OWNED_EQUIP(EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_MASTER) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_BIGGORON_SWORD:
            return !gSaveContext.bgsFlag ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_DEKU_SHIELD:
        case RG_BUY_DEKU_SHIELD:
            return !CHECK_OWNED_EQUIP(EQUIP_TYPE_SHIELD, EQUIP_INV_SHIELD_DEKU) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_HYLIAN_SHIELD:
        case RG_BUY_HYLIAN_SHIELD:
            return !CHECK_OWNED_EQUIP(EQUIP_TYPE_SHIELD, EQUIP_INV_SHIELD_HYLIAN) ? CAN_OBTAIN
                                                                                  : CANT_OBTAIN_ALREADY_HAVE;
        case RG_MIRROR_SHIELD:
            return !CHECK_OWNED_EQUIP(EQUIP_TYPE_SHIELD, EQUIP_INV_SHIELD_MIRROR) ? CAN_OBTAIN
                                                                                  : CANT_OBTAIN_ALREADY_HAVE;
        case RG_GORON_TUNIC:
        case RG_BUY_GORON_TUNIC:
            return !CHECK_OWNED_EQUIP(EQUIP_TYPE_TUNIC, EQUIP_INV_TUNIC_GORON) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_ZORA_TUNIC:
        case RG_BUY_ZORA_TUNIC:
            return !CHECK_OWNED_EQUIP(EQUIP_TYPE_TUNIC, EQUIP_INV_TUNIC_ZORA) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_IRON_BOOTS:
            return !CHECK_OWNED_EQUIP(EQUIP_TYPE_BOOTS, EQUIP_INV_BOOTS_IRON) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_HOVER_BOOTS:
            return !CHECK_OWNED_EQUIP(EQUIP_TYPE_BOOTS, EQUIP_INV_BOOTS_HOVER) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;

        // Inventory Items
        case RG_PROGRESSIVE_STICK_UPGRADE:
            return infiniteUpgrades != RO_INF_UPGRADES_OFF
                       ? (Flags_GetRandomizerInf(RAND_INF_HAS_INFINITE_STICK_UPGRADE) ? CANT_OBTAIN_ALREADY_HAVE
                                                                                      : CAN_OBTAIN)
                       : (CUR_UPG_VALUE(UPG_STICKS) < 3 ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE);
        case RG_DEKU_STICK_1:
        case RG_BUY_DEKU_STICK_1:
            return CUR_UPG_VALUE(UPG_STICKS) ||
                           !OTRGlobals::Instance->gRandoContext->GetOption(RSK_SHUFFLE_DEKU_STICK_BAG).Get()
                       ? CAN_OBTAIN
                       : CANT_OBTAIN_NEED_UPGRADE;
        case RG_PROGRESSIVE_NUT_UPGRADE:
            return infiniteUpgrades != RO_INF_UPGRADES_OFF
                       ? (Flags_GetRandomizerInf(RAND_INF_HAS_INFINITE_NUT_UPGRADE) ? CANT_OBTAIN_ALREADY_HAVE
                                                                                    : CAN_OBTAIN)
                       : (CUR_UPG_VALUE(UPG_NUTS) < 3 ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE);
        case RG_DEKU_NUTS_5:
        case RG_DEKU_NUTS_10:
        case RG_BUY_DEKU_NUTS_5:
        case RG_BUY_DEKU_NUTS_10:
            return CUR_UPG_VALUE(UPG_NUTS) ||
                           !OTRGlobals::Instance->gRandoContext->GetOption(RSK_SHUFFLE_DEKU_NUT_BAG).Get()
                       ? CAN_OBTAIN
                       : CANT_OBTAIN_NEED_UPGRADE;
        case RG_PROGRESSIVE_BOMB_BAG:
            return infiniteUpgrades != RO_INF_UPGRADES_OFF
                       ? (Flags_GetRandomizerInf(RAND_INF_HAS_INFINITE_BOMB_BAG) ? CANT_OBTAIN_ALREADY_HAVE
                                                                                 : CAN_OBTAIN)
                       : (CUR_UPG_VALUE(UPG_BOMB_BAG) < 3 ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE);
        case RG_BOMBS_5:
        case RG_BOMBS_10:
        case RG_BOMBS_20:
        case RG_BUY_BOMBS_525:
        case RG_BUY_BOMBS_535:
        case RG_BUY_BOMBS_10:
        case RG_BUY_BOMBS_20:
        case RG_BUY_BOMBS_30:
            return CUR_UPG_VALUE(UPG_BOMB_BAG) ? CAN_OBTAIN : CANT_OBTAIN_NEED_UPGRADE;
        case RG_PROGRESSIVE_BOW:
            return infiniteUpgrades != RO_INF_UPGRADES_OFF
                       ? (Flags_GetRandomizerInf(RAND_INF_HAS_INFINITE_QUIVER) ? CANT_OBTAIN_ALREADY_HAVE : CAN_OBTAIN)
                       : (CUR_UPG_VALUE(UPG_QUIVER) < 3 ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE);
        case RG_ARROWS_5:
        case RG_ARROWS_10:
        case RG_ARROWS_30:
        case RG_BUY_ARROWS_10:
        case RG_BUY_ARROWS_30:
        case RG_BUY_ARROWS_50:
            return CUR_UPG_VALUE(UPG_QUIVER) ? CAN_OBTAIN : CANT_OBTAIN_NEED_UPGRADE;
        case RG_PROGRESSIVE_SLINGSHOT:
            return infiniteUpgrades != RO_INF_UPGRADES_OFF
                       ? (Flags_GetRandomizerInf(RAND_INF_HAS_INFINITE_BULLET_BAG) ? CANT_OBTAIN_ALREADY_HAVE
                                                                                   : CAN_OBTAIN)
                       : (CUR_UPG_VALUE(UPG_BULLET_BAG) < 3 ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE);
        case RG_DEKU_SEEDS_30:
        case RG_BUY_DEKU_SEEDS_30:
            return CUR_UPG_VALUE(UPG_BULLET_BAG) ? CAN_OBTAIN : CANT_OBTAIN_NEED_UPGRADE;
        case RG_PROGRESSIVE_OCARINA:
            switch (INV_CONTENT(ITEM_OCARINA_FAIRY)) {
                case ITEM_NONE:
                case ITEM_OCARINA_FAIRY:
                    return CAN_OBTAIN;
                case ITEM_OCARINA_TIME:
                default:
                    return CANT_OBTAIN_ALREADY_HAVE;
            }
        case RG_BOMBCHU_5:
        case RG_BOMBCHU_10:
        case RG_BOMBCHU_20:
        case RG_BUY_BOMBCHUS_10:
        case RG_BUY_BOMBCHUS_20:
            return OTRGlobals::Instance->gRandoContext->GetOption(RSK_BOMBCHU_BAG).Is(RO_BOMBCHU_BAG_NONE)
                       ? CAN_OBTAIN
                       : (INV_CONTENT(ITEM_BOMBCHU) != ITEM_NONE ? CAN_OBTAIN : CANT_OBTAIN_NEED_UPGRADE);
        case RG_PROGRESSIVE_BOMBCHU_BAG: // RANDOTODO Do we want bombchu refills to exist separately from bombchu bags?
                                         // If so, this needs changing.
            switch (OTRGlobals::Instance->gRandoContext->GetOption(RSK_BOMBCHU_BAG).Get()) {
                case RO_BOMBCHU_BAG_NONE:
                    return CANT_OBTAIN_MISC;
                case RO_BOMBCHU_BAG_SINGLE:
                    return CAN_OBTAIN;
                case RO_BOMBCHU_BAG_PROGRESSIVE:
                    if (Flags_GetRandomizerInf(RAND_INF_HAS_INFINITE_BOMBCHUS)) {
                        return CANT_OBTAIN_ALREADY_HAVE;
                    } else {
                        switch (gSaveContext.ship.quest.data.randomizer.bombchuUpgradeLevel) {
                            case 0:
                            case 1:
                                return CAN_OBTAIN;
                            case 2:
                                return infiniteUpgrades == RO_INF_UPGRADES_CONDENSED_PROGRESSIVE
                                           ? CANT_OBTAIN_ALREADY_HAVE
                                           : CAN_OBTAIN;
                            case 3:
                                return infiniteUpgrades == RO_INF_UPGRADES_PROGRESSIVE ? CAN_OBTAIN
                                                                                       : CANT_OBTAIN_ALREADY_HAVE;
                        }
                    }
            }
            assert(false);
            return CAN_OBTAIN;
        case RG_PROGRESSIVE_HOOKSHOT:
            switch (INV_CONTENT(ITEM_HOOKSHOT)) {
                case ITEM_NONE:
                case ITEM_HOOKSHOT:
                    return CAN_OBTAIN;
                case ITEM_LONGSHOT:
                default:
                    return CANT_OBTAIN_ALREADY_HAVE;
            }
        case RG_BOOMERANG:
            return INV_CONTENT(ITEM_BOOMERANG) == ITEM_NONE ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_LENS_OF_TRUTH:
            return INV_CONTENT(ITEM_LENS) == ITEM_NONE ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_MAGIC_BEAN:
        case RG_MAGIC_BEAN_PACK:
            return AMMO(ITEM_BEAN) < 10 ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_MEGATON_HAMMER:
            return INV_CONTENT(ITEM_HAMMER) == ITEM_NONE ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_FIRE_ARROWS:
            return INV_CONTENT(ITEM_ARROW_FIRE) == ITEM_NONE ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_ICE_ARROWS:
            return INV_CONTENT(ITEM_ARROW_ICE) == ITEM_NONE ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_LIGHT_ARROWS:
            return INV_CONTENT(ITEM_ARROW_LIGHT) == ITEM_NONE ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_DINS_FIRE:
            return INV_CONTENT(ITEM_DINS_FIRE) == ITEM_NONE ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_FARORES_WIND:
            return INV_CONTENT(ITEM_FARORES_WIND) == ITEM_NONE ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_NAYRUS_LOVE:
            if (!GetRandoSettingValue(RSK_ROCS_FEATHER)) {
                return INV_CONTENT(ITEM_NAYRUS_LOVE) == ITEM_NONE ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
            } else {
                return Flags_GetRandomizerInf(RAND_INF_OBTAINED_NAYRUS_LOVE) ? CANT_OBTAIN_ALREADY_HAVE : CAN_OBTAIN;
            }
        case RG_ROCS_FEATHER:
            return Flags_GetRandomizerInf(RAND_INF_OBTAINED_ROCS_FEATHER) ? CANT_OBTAIN_ALREADY_HAVE : CAN_OBTAIN;

        // Bottles
        case RG_EMPTY_BOTTLE:
        case RG_BOTTLE_WITH_MILK:
        case RG_BOTTLE_WITH_RED_POTION:
        case RG_BOTTLE_WITH_GREEN_POTION:
        case RG_BOTTLE_WITH_BLUE_POTION:
        case RG_BOTTLE_WITH_FAIRY:
        case RG_BOTTLE_WITH_FISH:
        case RG_BOTTLE_WITH_BLUE_FIRE:
        case RG_BOTTLE_WITH_BUGS:
        case RG_BOTTLE_WITH_POE:
        case RG_RUTOS_LETTER:
        case RG_BOTTLE_WITH_BIG_POE:
        case RG_BOTTLE_WITH_MAGIC_MUSHROOM:
            return Inventory_HasEmptyBottleSlot() ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;

        // Bottle Refills
        case RG_MILK:
        case RG_FISH:
        case RG_RED_POTION_REFILL:
        case RG_GREEN_POTION_REFILL:
        case RG_BLUE_POTION_REFILL:
        case RG_BUY_FISH:
        case RG_BUY_RED_POTION_30:
        case RG_BUY_GREEN_POTION:
        case RG_BUY_BLUE_POTION:
        case RG_BUY_BLUE_FIRE:
        case RG_BUY_BOTTLE_BUG:
        case RG_BUY_POE:
        case RG_BUY_FAIRYS_SPIRIT:
        case RG_BUY_RED_POTION_40:
        case RG_BUY_RED_POTION_50:
            return Inventory_HasEmptyBottle() ? CAN_OBTAIN : CANT_OBTAIN_NEED_EMPTY_BOTTLE;

        // Trade Items
        // TODO: Do we want to be strict about any of this?
        // case RG_WEIRD_EGG:
        // case RG_ZELDAS_LETTER:
        // case RG_POCKET_EGG:
        // case RG_COJIRO:
        // case RG_ODD_MUSHROOM:
        // case RG_ODD_POTION:
        // case RG_POACHERS_SAW:
        // case RG_BROKEN_SWORD:
        // case RG_PRESCRIPTION:
        // case RG_EYEBALL_FROG:
        // case RG_EYEDROPS:
        // case RG_CLAIM_CHECK:
        // case RG_PROGRESSIVE_GORONSWORD:
        // case RG_GIANTS_KNIFE:

        // Misc Items
        case RG_STONE_OF_AGONY:
            return !CHECK_QUEST_ITEM(QUEST_STONE_OF_AGONY) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_GERUDO_MEMBERSHIP_CARD:
            return !CHECK_QUEST_ITEM(QUEST_GERUDO_CARD) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_DOUBLE_DEFENSE:
            return !gSaveContext.isDoubleDefenseAcquired ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_GOLD_SKULLTULA_TOKEN:
            return gSaveContext.inventory.gsTokens < 100 ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_PROGRESSIVE_STRENGTH:
            return CUR_UPG_VALUE(UPG_STRENGTH) < 3 ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_PROGRESSIVE_WALLET:
            return CUR_UPG_VALUE(UPG_WALLET) < numWallets ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_PROGRESSIVE_SCALE:
            return CUR_UPG_VALUE(UPG_SCALE) < 2 ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_PROGRESSIVE_MAGIC_METER:
        case RG_MAGIC_SINGLE:
        case RG_MAGIC_DOUBLE:
            return infiniteUpgrades != RO_INF_UPGRADES_OFF
                       ? (Flags_GetRandomizerInf(RAND_INF_HAS_INFINITE_MAGIC_METER) ? CANT_OBTAIN_ALREADY_HAVE
                                                                                    : CAN_OBTAIN)
                       : (gSaveContext.magicLevel < 2 ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE);
        case RG_FISHING_POLE:
            return !Flags_GetRandomizerInf(RAND_INF_FISHING_POLE_FOUND) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_PROGRESSIVE_ROCS:
            switch (gSaveContext.inventory.items[SLOT_ROCS]) {
                case ITEM_NONE:
                case ITEM_ROCS_FEATHER_SKIJER:
                    return CAN_OBTAIN;
                case ITEM_ROCS_CAPE:
                default:
                    return CANT_OBTAIN_ALREADY_HAVE;
            }

        // Songs
        case RG_ZELDAS_LULLABY:
            return !CHECK_QUEST_ITEM(QUEST_SONG_LULLABY) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_EPONAS_SONG:
            return !CHECK_QUEST_ITEM(QUEST_SONG_EPONA) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_SARIAS_SONG:
            return !CHECK_QUEST_ITEM(QUEST_SONG_SARIA) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_SUNS_SONG:
            return !CHECK_QUEST_ITEM(QUEST_SONG_SUN) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_SONG_OF_TIME:
            return !CHECK_QUEST_ITEM(QUEST_SONG_TIME) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_SONG_OF_STORMS:
            return !CHECK_QUEST_ITEM(QUEST_SONG_STORMS) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_MINUET_OF_FOREST:
            return !CHECK_QUEST_ITEM(QUEST_SONG_MINUET) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_BOLERO_OF_FIRE:
            return !CHECK_QUEST_ITEM(QUEST_SONG_BOLERO) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_SERENADE_OF_WATER:
            return !CHECK_QUEST_ITEM(QUEST_SONG_SERENADE) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_REQUIEM_OF_SPIRIT:
            return !CHECK_QUEST_ITEM(QUEST_SONG_REQUIEM) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_NOCTURNE_OF_SHADOW:
            return !CHECK_QUEST_ITEM(QUEST_SONG_NOCTURNE) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_PRELUDE_OF_LIGHT:
            return !CHECK_QUEST_ITEM(QUEST_SONG_PRELUDE) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;

        // Dungeon Items
        case RG_DEKU_TREE_MAP:
            return !CHECK_DUNGEON_ITEM(DUNGEON_MAP, SCENE_DEKU_TREE) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_DODONGOS_CAVERN_MAP:
            return !CHECK_DUNGEON_ITEM(DUNGEON_MAP, SCENE_DODONGOS_CAVERN) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_JABU_JABUS_BELLY_MAP:
            return !CHECK_DUNGEON_ITEM(DUNGEON_MAP, SCENE_JABU_JABU) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_FOREST_TEMPLE_MAP:
            return !CHECK_DUNGEON_ITEM(DUNGEON_MAP, SCENE_FOREST_TEMPLE) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_FIRE_TEMPLE_MAP:
            return !CHECK_DUNGEON_ITEM(DUNGEON_MAP, SCENE_FIRE_TEMPLE) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_WATER_TEMPLE_MAP:
            return !CHECK_DUNGEON_ITEM(DUNGEON_MAP, SCENE_WATER_TEMPLE) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_SPIRIT_TEMPLE_MAP:
            return !CHECK_DUNGEON_ITEM(DUNGEON_MAP, SCENE_SPIRIT_TEMPLE) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_SHADOW_TEMPLE_MAP:
            return !CHECK_DUNGEON_ITEM(DUNGEON_MAP, SCENE_SHADOW_TEMPLE) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_BOTTOM_OF_THE_WELL_MAP:
            return !CHECK_DUNGEON_ITEM(DUNGEON_MAP, SCENE_BOTTOM_OF_THE_WELL) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_ICE_CAVERN_MAP:
            return !CHECK_DUNGEON_ITEM(DUNGEON_MAP, SCENE_ICE_CAVERN) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_DEKU_TREE_COMPASS:
            return !CHECK_DUNGEON_ITEM(DUNGEON_COMPASS, SCENE_DEKU_TREE) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_DODONGOS_CAVERN_COMPASS:
            return !CHECK_DUNGEON_ITEM(DUNGEON_COMPASS, SCENE_DODONGOS_CAVERN) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_JABU_JABUS_BELLY_COMPASS:
            return !CHECK_DUNGEON_ITEM(DUNGEON_COMPASS, SCENE_JABU_JABU) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_FOREST_TEMPLE_COMPASS:
            return !CHECK_DUNGEON_ITEM(DUNGEON_COMPASS, SCENE_FOREST_TEMPLE) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_FIRE_TEMPLE_COMPASS:
            return !CHECK_DUNGEON_ITEM(DUNGEON_COMPASS, SCENE_FIRE_TEMPLE) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_WATER_TEMPLE_COMPASS:
            return !CHECK_DUNGEON_ITEM(DUNGEON_COMPASS, SCENE_WATER_TEMPLE) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_SPIRIT_TEMPLE_COMPASS:
            return !CHECK_DUNGEON_ITEM(DUNGEON_COMPASS, SCENE_SPIRIT_TEMPLE) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_SHADOW_TEMPLE_COMPASS:
            return !CHECK_DUNGEON_ITEM(DUNGEON_COMPASS, SCENE_SHADOW_TEMPLE) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_BOTTOM_OF_THE_WELL_COMPASS:
            return !CHECK_DUNGEON_ITEM(DUNGEON_COMPASS, SCENE_BOTTOM_OF_THE_WELL) ? CAN_OBTAIN
                                                                                  : CANT_OBTAIN_ALREADY_HAVE;
        case RG_ICE_CAVERN_COMPASS:
            return !CHECK_DUNGEON_ITEM(DUNGEON_COMPASS, SCENE_ICE_CAVERN) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_FOREST_TEMPLE_BOSS_KEY:
            return !CHECK_DUNGEON_ITEM(DUNGEON_KEY_BOSS, SCENE_FOREST_TEMPLE) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_FIRE_TEMPLE_BOSS_KEY:
            return !CHECK_DUNGEON_ITEM(DUNGEON_KEY_BOSS, SCENE_FIRE_TEMPLE) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_WATER_TEMPLE_BOSS_KEY:
            return !CHECK_DUNGEON_ITEM(DUNGEON_KEY_BOSS, SCENE_WATER_TEMPLE) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_SPIRIT_TEMPLE_BOSS_KEY:
            return !CHECK_DUNGEON_ITEM(DUNGEON_KEY_BOSS, SCENE_SPIRIT_TEMPLE) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_SHADOW_TEMPLE_BOSS_KEY:
            return !CHECK_DUNGEON_ITEM(DUNGEON_KEY_BOSS, SCENE_SHADOW_TEMPLE) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_GANONS_CASTLE_BOSS_KEY:
            return !CHECK_DUNGEON_ITEM(DUNGEON_KEY_BOSS, SCENE_GANONS_TOWER) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_FOREST_TEMPLE_SMALL_KEY:
            return gSaveContext.inventory.dungeonKeys[SCENE_FOREST_TEMPLE] < FOREST_TEMPLE_SMALL_KEY_MAX
                       ? CAN_OBTAIN
                       : CANT_OBTAIN_ALREADY_HAVE;
        case RG_FIRE_TEMPLE_SMALL_KEY:
            return gSaveContext.inventory.dungeonKeys[SCENE_FIRE_TEMPLE] < FIRE_TEMPLE_SMALL_KEY_MAX
                       ? CAN_OBTAIN
                       : CANT_OBTAIN_ALREADY_HAVE;
        case RG_WATER_TEMPLE_SMALL_KEY:
            return gSaveContext.inventory.dungeonKeys[SCENE_WATER_TEMPLE] < WATER_TEMPLE_SMALL_KEY_MAX
                       ? CAN_OBTAIN
                       : CANT_OBTAIN_ALREADY_HAVE;
        case RG_SPIRIT_TEMPLE_SMALL_KEY:
            return gSaveContext.inventory.dungeonKeys[SCENE_SPIRIT_TEMPLE] < SPIRIT_TEMPLE_SMALL_KEY_MAX
                       ? CAN_OBTAIN
                       : CANT_OBTAIN_ALREADY_HAVE;
        case RG_SHADOW_TEMPLE_SMALL_KEY:
            return gSaveContext.inventory.dungeonKeys[SCENE_SHADOW_TEMPLE] < SHADOW_TEMPLE_SMALL_KEY_MAX
                       ? CAN_OBTAIN
                       : CANT_OBTAIN_ALREADY_HAVE;
        case RG_BOTTOM_OF_THE_WELL_SMALL_KEY:
            return gSaveContext.inventory.dungeonKeys[SCENE_BOTTOM_OF_THE_WELL] < BOTTOM_OF_THE_WELL_SMALL_KEY_MAX
                       ? CAN_OBTAIN
                       : CANT_OBTAIN_ALREADY_HAVE;
        case RG_GERUDO_TRAINING_GROUND_SMALL_KEY:
            return gSaveContext.inventory.dungeonKeys[SCENE_GERUDO_TRAINING_GROUND] <
                           GERUDO_TRAINING_GROUND_SMALL_KEY_MAX
                       ? CAN_OBTAIN
                       : CANT_OBTAIN_ALREADY_HAVE;
        case RG_GERUDO_FORTRESS_SMALL_KEY:
            return gSaveContext.inventory.dungeonKeys[SCENE_THIEVES_HIDEOUT] < GERUDO_FORTRESS_SMALL_KEY_MAX
                       ? CAN_OBTAIN
                       : CANT_OBTAIN_ALREADY_HAVE;
        case RG_GANONS_CASTLE_SMALL_KEY:
            return gSaveContext.inventory.dungeonKeys[SCENE_INSIDE_GANONS_CASTLE] < GANONS_CASTLE_SMALL_KEY_MAX
                       ? CAN_OBTAIN
                       : CANT_OBTAIN_ALREADY_HAVE;
        case RG_TREASURE_GAME_SMALL_KEY:
            return gSaveContext.inventory.dungeonKeys[SCENE_TREASURE_BOX_SHOP] < TREASURE_GAME_SMALL_KEY_MAX
                       ? CAN_OBTAIN
                       : CANT_OBTAIN_ALREADY_HAVE;

        // Dungeon Rewards
        case RG_KOKIRI_EMERALD:
            return !CHECK_QUEST_ITEM(QUEST_KOKIRI_EMERALD) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_GORON_RUBY:
            return !CHECK_QUEST_ITEM(QUEST_GORON_RUBY) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_ZORA_SAPPHIRE:
            return !CHECK_QUEST_ITEM(QUEST_ZORA_SAPPHIRE) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_FOREST_MEDALLION:
            return !CHECK_QUEST_ITEM(QUEST_MEDALLION_FOREST) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_FIRE_MEDALLION:
            return !CHECK_QUEST_ITEM(QUEST_MEDALLION_FIRE) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_WATER_MEDALLION:
            return !CHECK_QUEST_ITEM(QUEST_MEDALLION_WATER) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_SPIRIT_MEDALLION:
            return !CHECK_QUEST_ITEM(QUEST_MEDALLION_SPIRIT) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_SHADOW_MEDALLION:
            return !CHECK_QUEST_ITEM(QUEST_MEDALLION_SHADOW) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;
        case RG_LIGHT_MEDALLION:
            return !CHECK_QUEST_ITEM(QUEST_MEDALLION_LIGHT) ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE;

        case RG_RECOVERY_HEART:
        case RG_GREEN_RUPEE:
        case RG_GREG_RUPEE:
        case RG_BLUE_RUPEE:
        case RG_RED_RUPEE:
        case RG_PURPLE_RUPEE:
        case RG_HUGE_RUPEE:
        case RG_PIECE_OF_HEART:
        case RG_HEART_CONTAINER:
        case RG_ICE_TRAP:
        case RG_TREASURE_GAME_HEART:
        case RG_TREASURE_GAME_GREEN_RUPEE:
        case RG_BUY_HEART:
        case RG_TRIFORCE_PIECE:
        default:
            return CAN_OBTAIN;
    }
}

Rando::Location* Randomizer::GetCheckObjectFromActor(s16 actorId, s16 sceneNum, s32 actorParams = 0x00) {
    RandomizerCheck specialRc = RC_UNKNOWN_CHECK;
    // TODO: Migrate these special cases into table, or at least document why they are special
    switch (sceneNum) {
        case SCENE_TREASURE_BOX_SHOP: {
            if ((actorId == ACTOR_EN_BOX && actorParams == 20170) ||
                (actorId == ACTOR_ITEM_ETCETERA && actorParams == 2572)) {
                specialRc = RC_MARKET_TREASURE_CHEST_GAME_REWARD;
            }

            // todo: handle the itemetc part of this so drawing works when we implement shuffle
            if (actorId == ACTOR_EN_BOX) {
                bool isAKey = (actorParams & 0x60) == 0x20;
                if ((actorParams & 0xF) < 2) {
                    specialRc = isAKey ? RC_MARKET_TREASURE_CHEST_GAME_KEY_1 : RC_MARKET_TREASURE_CHEST_GAME_ITEM_1;
                } else if ((actorParams & 0xF) < 4) {
                    specialRc = isAKey ? RC_MARKET_TREASURE_CHEST_GAME_KEY_2 : RC_MARKET_TREASURE_CHEST_GAME_ITEM_2;
                } else if ((actorParams & 0xF) < 6) {
                    specialRc = isAKey ? RC_MARKET_TREASURE_CHEST_GAME_KEY_3 : RC_MARKET_TREASURE_CHEST_GAME_ITEM_3;
                } else if ((actorParams & 0xF) < 8) {
                    specialRc = isAKey ? RC_MARKET_TREASURE_CHEST_GAME_KEY_4 : RC_MARKET_TREASURE_CHEST_GAME_ITEM_4;
                } else if ((actorParams & 0xF) < 10) {
                    specialRc = isAKey ? RC_MARKET_TREASURE_CHEST_GAME_KEY_5 : RC_MARKET_TREASURE_CHEST_GAME_ITEM_5;
                }
            }
            break;
        }
        case SCENE_SACRED_FOREST_MEADOW:
            if (actorId == ACTOR_EN_SA) {
                specialRc = RC_SONG_FROM_SARIA;
            }
            break;
        case SCENE_TEMPLE_OF_TIME_EXTERIOR_DAY:
        case SCENE_TEMPLE_OF_TIME_EXTERIOR_NIGHT:
        case SCENE_TEMPLE_OF_TIME_EXTERIOR_RUINS:
            switch (actorParams) {
                case 14342:
                    specialRc = RC_TOT_LEFTMOST_GOSSIP_STONE;
                    break;
                case 14599:
                    specialRc = RC_TOT_LEFT_CENTER_GOSSIP_STONE;
                    break;
                case 14862:
                    specialRc = RC_TOT_RIGHT_CENTER_GOSSIP_STONE;
                    break;
                case 15120:
                    specialRc = RC_TOT_RIGHTMOST_GOSSIP_STONE;
                    break;
            }
            break;
        case SCENE_HOUSE_OF_SKULLTULA:
            if (actorId == ACTOR_EN_SSH) {
                switch (actorParams) { // actor params are used to differentiate between textboxes
                    case 1:
                        specialRc = RC_KAK_10_GOLD_SKULLTULA_REWARD;
                        break;
                    case 2:
                        specialRc = RC_KAK_20_GOLD_SKULLTULA_REWARD;
                        break;
                    case 3:
                        specialRc = RC_KAK_30_GOLD_SKULLTULA_REWARD;
                        break;
                    case 4:
                        specialRc = RC_KAK_40_GOLD_SKULLTULA_REWARD;
                        break;
                    case 5:
                        specialRc = RC_KAK_50_GOLD_SKULLTULA_REWARD;
                        break;
                }
            }
            break;
        case SCENE_KAKARIKO_VILLAGE:
            switch (actorId) {
                case ACTOR_EN_NIW_LADY:
                    if (LINK_IS_ADULT) {
                        specialRc = RC_KAK_ANJU_AS_ADULT;
                    } else {
                        specialRc = RC_KAK_ANJU_AS_CHILD;
                    }
            }
            break;
        case SCENE_LAKE_HYLIA:
            switch (actorId) {
                case ACTOR_ITEM_ETCETERA:
                    if (LINK_IS_ADULT) {
                        specialRc = RC_LH_SUN;
                    } else {
                        specialRc = RC_LH_UNDERWATER_ITEM;
                    }
            }
            break;
        case SCENE_ZORAS_FOUNTAIN:
            switch (actorParams) {
                case 15362:
                case 14594:
                    specialRc = RC_ZF_JABU_GOSSIP_STONE;
                    break;
                case 14849:
                case 14337:
                    specialRc = RC_ZF_FAIRY_GOSSIP_STONE;
                    break;
            }
            break;
        case SCENE_GERUDOS_FORTRESS:
            // GF chest as child has different params and gives odd mushroom
            // set it to the GF chest check for both ages
            if (actorId == ACTOR_EN_BOX) {
                specialRc = RC_GF_CHEST;
            }
            break;
        case SCENE_DODONGOS_CAVERN:
            // special case for MQ DC Gossip Stone
            if (actorId == ACTOR_EN_GS && actorParams == 15892 && ResourceMgr_IsGameMasterQuest()) {
                specialRc = RC_DODONGOS_CAVERN_GOSSIP_STONE;
            }
            break;
        case SCENE_SHOOTING_GALLERY:
            // special case for shooting gallery sign
            if (actorId == ACTOR_EN_KANBAN) {
                if (LINK_IS_ADULT) {
                    specialRc = RC_KAK_SHOOTING_GALLERY_RECTANGLE_SIGN;
                } else {
                    specialRc = RC_MK_SHOOTING_GALLERY_RECTANGLE_SIGN;
                }
            }
            break;
    }

    if (specialRc != RC_UNKNOWN_CHECK) {
        return Rando::StaticData::GetLocation(specialRc);
    }

    auto range = Rando::StaticData::CheckFromActorMultimap.equal_range(std::make_tuple(actorId, sceneNum, actorParams));

    for (auto it = range.first; it != range.second; ++it) {
        if (Rando::StaticData::GetLocation(it->second)->GetQuest() == RCQUEST_BOTH ||
            (Rando::StaticData::GetLocation(it->second)->GetQuest() == RCQUEST_VANILLA &&
             !ResourceMgr_IsGameMasterQuest()) ||
            (Rando::StaticData::GetLocation(it->second)->GetQuest() == RCQUEST_MQ && ResourceMgr_IsGameMasterQuest())) {
            return Rando::StaticData::GetLocation(it->second);
        }
    }

    return Rando::StaticData::GetLocation(RC_UNKNOWN_CHECK);
}

// RANDOTODO: Move all Shopsanity stuff to a ShuffleShops.cpp
ShopItemIdentity Randomizer::IdentifyShopItem(s32 sceneNum, u8 slotIndex) {
    ShopItemIdentity shopItemIdentity;

    shopItemIdentity.identity.randomizerInf = RAND_INF_MAX;
    shopItemIdentity.identity.randomizerCheck = RC_UNKNOWN_CHECK;
    shopItemIdentity.ogItemId = GI_NONE;
    shopItemIdentity.itemPrice = -1;
    shopItemIdentity.enGirlAShopItem = 0x32;

    if (slotIndex == 0) {
        return shopItemIdentity;
    }

    Rando::Location* location = GetCheckObjectFromActor(
        ACTOR_EN_GIRLA,
        // Bazaar (SHOP1) scene is reused, so if entering from Kak use debug scene to identify
        (sceneNum == SCENE_BAZAAR && gSaveContext.entranceIndex == ENTR_BAZAAR_0) ? SCENE_TEST01 : sceneNum,
        slotIndex - 1);

    if (location->GetRandomizerCheck() != RC_UNKNOWN_CHECK) {
        shopItemIdentity.identity.randomizerInf = rcToRandomizerInf[location->GetRandomizerCheck()];
        shopItemIdentity.identity.randomizerCheck = location->GetRandomizerCheck();
        shopItemIdentity.ogItemId = (GetItemID)Rando::StaticData::RetrieveItem(location->GetVanillaItem()).GetItemID();

        RandomizerGet randoGet = Rando::Context::GetInstance()
                                     ->GetItemLocation(shopItemIdentity.identity.randomizerCheck)
                                     ->GetPlacedRandomizerGet();
        if (randomizerGetToEnGirlShopItem.find(randoGet) != randomizerGetToEnGirlShopItem.end()) {
            shopItemIdentity.enGirlAShopItem = randomizerGetToEnGirlShopItem[randoGet];
        }

        shopItemIdentity.itemPrice =
            OTRGlobals::Instance->gRandoContext->GetItemLocation(shopItemIdentity.identity.randomizerCheck)->GetPrice();
    }

    return shopItemIdentity;
}

u8 Randomizer::GetRandoSettingValue(RandomizerSettingKey randoSettingKey) {
    return Rando::Context::GetInstance()->GetOption(randoSettingKey).Get();
}

GetItemEntry Randomizer::GetItemFromKnownCheck(RandomizerCheck randomizerCheck, GetItemID ogItemId,
                                               bool checkObtainability) {
    return Rando::Context::GetInstance()->GetFinalGIEntry(randomizerCheck, checkObtainability);
}

RandomizerCheck Randomizer::GetCheckFromActor(s16 actorId, s16 sceneNum, s16 actorParams) {
    return GetCheckObjectFromActor(actorId, sceneNum, actorParams)->GetRandomizerCheck();
}

RandomizerInf Randomizer::GetRandomizerInfFromCheck(RandomizerCheck rc) {
    auto rcIt = rcToRandomizerInf.find(rc);
    if (rcIt == rcToRandomizerInf.end())
        return RAND_INF_MAX;

    return rcIt->second;
}

RandomizerCheck Randomizer::GetCheckFromRandomizerInf(RandomizerInf randomizerInf) {
    for (auto const& [key, value] : rcToRandomizerInf) {
        if (value == randomizerInf)
            return key;
    }

    return RC_UNKNOWN_CHECK;
}

std::thread randoThread;

void GenerateRandomizerImgui(std::string seed = "") {
    CVarSetInteger(CVAR_GENERAL("RandoGenerating"), 1);
    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    auto ctx = Rando::Context::GetInstance();
    // RANDOTODO proper UI for selecting if a spoiler loaded should be used for settings
    Rando::Settings::GetInstance()->SetAllToContext();

    // todo: this efficiently when we build out cvar array support
    std::set<RandomizerCheck> excludedLocations;
    std::stringstream excludedLocationStringStream(CVarGetString(CVAR_RANDOMIZER_SETTING("ExcludedLocations"), ""));
    std::string excludedLocationString;
    while (getline(excludedLocationStringStream, excludedLocationString, ',')) {
        excludedLocations.insert((RandomizerCheck)std::stoi(excludedLocationString));
    }

    // todo: better way to sort out linking tricks rather than name

    std::set<RandomizerTrick> enabledTricks;
    std::stringstream enabledTrickStringStream(CVarGetString(CVAR_RANDOMIZER_SETTING("EnabledTricks"), ""));
    std::string enabledTrickString;
    while (getline(enabledTrickStringStream, enabledTrickString, ',')) {
        if (Rando::StaticData::trickToEnum.contains(enabledTrickString)) {
            enabledTricks.insert(Rando::StaticData::trickToEnum[enabledTrickString]);
        }
    }

    // Update the visibilitiy before removing conflicting excludes (in case the locations tab wasn't viewed)
    RandomizerCheckObjects::UpdateImGuiVisibility();

    // Remove excludes for locations that are no longer allowed to be excluded
    for (auto& location : Rando::StaticData::GetLocationTable()) {
        auto elfound = excludedLocations.find(location.GetRandomizerCheck());
        if (!ctx->GetItemLocation(location.GetRandomizerCheck())->IsVisible() && elfound != excludedLocations.end()) {
            excludedLocations.erase(elfound);
        }
    }

    Rando::Context::GetInstance()->SetSeedGenerated(GenerateRandomizer(excludedLocations, enabledTricks, seed));
    CVarSetInteger(CVAR_GENERAL("RandoGenerating"), 0);
    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();

    generated = true;

    GameInteractor::Instance->ExecuteHooks<GameInteractor::OnGenerationCompletion>();
}

bool GenerateRandomizer(std::string seed /*= ""*/) {
    if (generated) {
        generated = false;
        randoThread.join();
    }
    if (CVarGetInteger(CVAR_GENERAL("RandoGenerating"), 0) == 0) {
        randoThread = std::thread(&GenerateRandomizerImgui, seed);
        return true;
    }
    return false;
}

static bool locationsTabOpen = false;
static bool tricksTabOpen = false;

void JoinRandoGenerationThread() {
    if (generated) {
        generated = false;
        randoThread.join();
    }
}

class ExtendedVanillaTableInvalidItemIdException : public std::exception {
  private:
    s16 itemID;

  public:
    ExtendedVanillaTableInvalidItemIdException(s16 itemID) : itemID(itemID) {
    }
    std::string what() {
        return itemID + " is not a valid ItemID for the extendedVanillaGetItemTable. If you are adding a new"
                        "item, try adding it to randoGetItemTable instead.";
    }
};

static std::unordered_map<RandomizerGet, GameplayStatTimestamp> randomizerGetToStatsTimeStamp = {
    { RG_GOHMA_SOUL, TIMESTAMP_FOUND_GOHMA_SOUL },
    { RG_KING_DODONGO_SOUL, TIMESTAMP_FOUND_KING_DODONGO_SOUL },
    { RG_BARINADE_SOUL, TIMESTAMP_FOUND_BARINADE_SOUL },
    { RG_PHANTOM_GANON_SOUL, TIMESTAMP_FOUND_PHANTOM_GANON_SOUL },
    { RG_VOLVAGIA_SOUL, TIMESTAMP_FOUND_VOLVAGIA_SOUL },
    { RG_MORPHA_SOUL, TIMESTAMP_FOUND_MORPHA_SOUL },
    { RG_BONGO_BONGO_SOUL, TIMESTAMP_FOUND_BONGO_BONGO_SOUL },
    { RG_TWINROVA_SOUL, TIMESTAMP_FOUND_TWINROVA_SOUL },
    { RG_GANON_SOUL, TIMESTAMP_FOUND_GANON_SOUL },

    { RG_BRONZE_SCALE, TIMESTAMP_FOUND_BRONZE_SCALE },

    { RG_OCARINA_A_BUTTON, TIMESTAMP_FOUND_OCARINA_A_BUTTON },
    { RG_OCARINA_C_UP_BUTTON, TIMESTAMP_FOUND_OCARINA_C_UP_BUTTON },
    { RG_OCARINA_C_DOWN_BUTTON, TIMESTAMP_FOUND_OCARINA_C_DOWN_BUTTON },
    { RG_OCARINA_C_LEFT_BUTTON, TIMESTAMP_FOUND_OCARINA_C_LEFT_BUTTON },
    { RG_OCARINA_C_RIGHT_BUTTON, TIMESTAMP_FOUND_OCARINA_C_RIGHT_BUTTON },

    { RG_FISHING_POLE, TIMESTAMP_FOUND_FISHING_POLE },

    { RG_GUARD_HOUSE_KEY, TIMESTAMP_FOUND_GUARD_HOUSE_KEY },
    { RG_MARKET_BAZAAR_KEY, TIMESTAMP_FOUND_MARKET_BAZAAR_KEY },
    { RG_MARKET_POTION_SHOP_KEY, TIMESTAMP_FOUND_MARKET_POTION_SHOP_KEY },
    { RG_MASK_SHOP_KEY, TIMESTAMP_FOUND_MASK_SHOP_KEY },
    { RG_MARKET_SHOOTING_GALLERY_KEY, TIMESTAMP_FOUND_MARKET_SHOOTING_GALLERY_KEY },
    { RG_BOMBCHU_BOWLING_KEY, TIMESTAMP_FOUND_BOMBCHU_BOWLING_KEY },
    { RG_TREASURE_CHEST_GAME_BUILDING_KEY, TIMESTAMP_FOUND_TREASURE_CHEST_GAME_BUILDING_KEY },
    { RG_BOMBCHU_SHOP_KEY, TIMESTAMP_FOUND_BOMBCHU_SHOP_KEY },
    { RG_RICHARDS_HOUSE_KEY, TIMESTAMP_FOUND_RICHARDS_HOUSE_KEY },
    { RG_ALLEY_HOUSE_KEY, TIMESTAMP_FOUND_ALLEY_HOUSE_KEY },
    { RG_KAK_BAZAAR_KEY, TIMESTAMP_FOUND_KAK_BAZAAR_KEY },
    { RG_KAK_POTION_SHOP_KEY, TIMESTAMP_FOUND_KAK_POTION_SHOP_KEY },
    { RG_BOSS_HOUSE_KEY, TIMESTAMP_FOUND_BOSS_HOUSE_KEY },
    { RG_GRANNYS_POTION_SHOP_KEY, TIMESTAMP_FOUND_GRANNYS_POTION_SHOP_KEY },
    { RG_SKULLTULA_HOUSE_KEY, TIMESTAMP_FOUND_SKULLTULA_HOUSE_KEY },
    { RG_IMPAS_HOUSE_KEY, TIMESTAMP_FOUND_IMPAS_HOUSE_KEY },
    { RG_WINDMILL_KEY, TIMESTAMP_FOUND_WINDMILL_KEY },
    { RG_KAK_SHOOTING_GALLERY_KEY, TIMESTAMP_FOUND_KAK_SHOOTING_GALLERY_KEY },
    { RG_DAMPES_HUT_KEY, TIMESTAMP_FOUND_DAMPES_HUT_KEY },
    { RG_TALONS_HOUSE_KEY, TIMESTAMP_FOUND_TALONS_HOUSE_KEY },
    { RG_STABLES_KEY, TIMESTAMP_FOUND_STABLES_KEY },
    { RG_BACK_TOWER_KEY, TIMESTAMP_FOUND_BACK_TOWER_KEY },
    { RG_HYLIA_LAB_KEY, TIMESTAMP_FOUND_HYLIA_LAB_KEY },
    { RG_FISHING_HOLE_KEY, TIMESTAMP_FOUND_FISHING_HOLE_KEY },
};

// Gameplay stat tracking: Update time the item was acquired
// (special cases for rando items)
void Randomizer_GameplayStats_SetTimestamp(uint16_t item) {
    u32 time = static_cast<u32>(GAMEPLAYSTAT_TOTAL_TIME);
    // Have items in Link's pocket shown as being obtained at 0.1 seconds
    if (time == 0) {
        time = 1;
    }

    if (gSaveContext.ship.stats.itemTimestamp[item] == 0) {
        if (item == RG_GANONS_CASTLE_BOSS_KEY) {
            gSaveContext.ship.stats.itemTimestamp[ITEM_KEY_BOSS] = time;
        } else if (item == RG_MASTER_SWORD) {
            gSaveContext.ship.stats.itemTimestamp[ITEM_SWORD_MASTER] = time;
        } else if (randomizerGetToStatsTimeStamp.contains((RandomizerGet)item)) {
            gSaveContext.ship.stats.itemTimestamp[randomizerGetToStatsTimeStamp[(RandomizerGet)item]] = time;
        } else if (item >= RG_EMPTY_BOTTLE && item <= RG_BOTTLE_WITH_BIG_POE) {
            gSaveContext.ship.stats.itemTimestamp[ITEM_BOTTLE] = time;
        } else if ((item >= RG_BOMBCHU_5 && item <= RG_BOMBCHU_20) || item == RG_PROGRESSIVE_BOMBCHU_BAG) {
            gSaveContext.ship.stats.itemTimestamp[ITEM_BOMBCHU] = time;
        } else if (item == RG_MAGIC_SINGLE) {
            gSaveContext.ship.stats.itemTimestamp[ITEM_SINGLE_MAGIC] = time;
        } else if (item == RG_DOUBLE_DEFENSE) {
            gSaveContext.ship.stats.itemTimestamp[ITEM_DOUBLE_DEFENSE] = time;
        }
    }
}

extern "C" u8 Return_Item_Entry(GetItemEntry itemEntry, u8 returnItem);

extern "C" u16 Randomizer_Item_Give(PlayState* play, GetItemEntry giEntry) {
    if (giEntry.modIndex != MOD_RANDOMIZER) {
        LUSLOG_WARN(
            "Randomizer_Item_Give was called with a GetItemEntry with a mod index different from MOD_RANDOMIZER (%d)",
            giEntry.modIndex);
        assert(false);
        return -1;
    }

    RandomizerGet item = (RandomizerGet)giEntry.getItemId;

    // Gameplay stats: Update the time the item was obtained
    Randomizer_GameplayStats_SetTimestamp(item);

    // if it's an item that just sets a randomizerInf, set it
    if (randomizerGetToRandInf.find(item) != randomizerGetToRandInf.end()) {
        Flags_SetRandomizerInf(randomizerGetToRandInf.find(item)->second);
        return Return_Item_Entry(giEntry, RG_NONE);
    }

    // bottle items
    if (item >= RG_BOTTLE_WITH_RED_POTION && item <= RG_BOTTLE_WITH_BIG_POE) {
        for (u16 i = 0; i < 4; i++) {
            if (gSaveContext.inventory.items[SLOT_BOTTLE_1 + i] == ITEM_NONE) {
                ItemID bottleItem = ITEM_NONE;
                switch (item) {
                    case RG_BOTTLE_WITH_RED_POTION:
                        bottleItem = ITEM_POTION_RED;
                        break;
                    case RG_BOTTLE_WITH_GREEN_POTION:
                        bottleItem = ITEM_POTION_GREEN;
                        break;
                    case RG_BOTTLE_WITH_BLUE_POTION:
                        bottleItem = ITEM_POTION_BLUE;
                        break;
                    case RG_BOTTLE_WITH_FAIRY:
                        bottleItem = ITEM_FAIRY;
                        break;
                    case RG_BOTTLE_WITH_FISH:
                        bottleItem = ITEM_FISH;
                        break;
                    case RG_BOTTLE_WITH_BLUE_FIRE:
                        bottleItem = ITEM_BLUE_FIRE;
                        break;
                    case RG_BOTTLE_WITH_BUGS:
                        bottleItem = ITEM_BUG;
                        break;
                    case RG_BOTTLE_WITH_POE:
                        bottleItem = ITEM_POE;
                        break;
                    case RG_BOTTLE_WITH_BIG_POE:
                        bottleItem = ITEM_BIG_POE;
                        break;
                    default:
                        break;
                }

                gSaveContext.inventory.items[SLOT_BOTTLE_1 + i] = bottleItem;
                return Return_Item_Entry(giEntry, RG_NONE);
            }
        }
    }

    // Magic Mushroom bottle (NEI custom - not part of the vanilla bottle
    // range, so handled separately).
    if (item == RG_BOTTLE_WITH_MAGIC_MUSHROOM) {
        for (u16 i = 0; i < 4; i++) {
            if (gSaveContext.inventory.items[SLOT_BOTTLE_1 + i] == ITEM_NONE) {
                gSaveContext.inventory.items[SLOT_BOTTLE_1 + i] = ITEM_BOTTLE_WITH_MAGIC_MUSHROOM;
                return Return_Item_Entry(giEntry, RG_NONE);
            }
        }
    }

    // dungeon items
    if ((item >= RG_FOREST_TEMPLE_SMALL_KEY && item <= RG_GANONS_CASTLE_SMALL_KEY) ||
        (item >= RG_FOREST_TEMPLE_KEY_RING && item <= RG_GANONS_CASTLE_KEY_RING) ||
        (item >= RG_FOREST_TEMPLE_BOSS_KEY && item <= RG_GANONS_CASTLE_BOSS_KEY) ||
        (item >= RG_DEKU_TREE_MAP && item <= RG_ICE_CAVERN_MAP) ||
        (item >= RG_DEKU_TREE_COMPASS && item <= RG_ICE_CAVERN_COMPASS)) {
        u16 mapIndex = gSaveContext.mapIndex;
        u8 numOfKeysOnKeyring = 0;
        switch (item) {
            case RG_DEKU_TREE_MAP:
            case RG_DEKU_TREE_COMPASS:
                mapIndex = SCENE_DEKU_TREE;
                break;
            case RG_DODONGOS_CAVERN_MAP:
            case RG_DODONGOS_CAVERN_COMPASS:
                mapIndex = SCENE_DODONGOS_CAVERN;
                break;
            case RG_JABU_JABUS_BELLY_MAP:
            case RG_JABU_JABUS_BELLY_COMPASS:
                mapIndex = SCENE_JABU_JABU;
                break;
            case RG_FOREST_TEMPLE_MAP:
            case RG_FOREST_TEMPLE_COMPASS:
            case RG_FOREST_TEMPLE_SMALL_KEY:
            case RG_FOREST_TEMPLE_KEY_RING:
            case RG_FOREST_TEMPLE_BOSS_KEY:
                mapIndex = SCENE_FOREST_TEMPLE;
                numOfKeysOnKeyring = FOREST_TEMPLE_SMALL_KEY_MAX;
                break;
            case RG_FIRE_TEMPLE_MAP:
            case RG_FIRE_TEMPLE_COMPASS:
            case RG_FIRE_TEMPLE_SMALL_KEY:
            case RG_FIRE_TEMPLE_KEY_RING:
            case RG_FIRE_TEMPLE_BOSS_KEY:
                mapIndex = SCENE_FIRE_TEMPLE;
                numOfKeysOnKeyring = FIRE_TEMPLE_SMALL_KEY_MAX;
                break;
            case RG_WATER_TEMPLE_MAP:
            case RG_WATER_TEMPLE_COMPASS:
            case RG_WATER_TEMPLE_SMALL_KEY:
            case RG_WATER_TEMPLE_KEY_RING:
            case RG_WATER_TEMPLE_BOSS_KEY:
                mapIndex = SCENE_WATER_TEMPLE;
                numOfKeysOnKeyring = WATER_TEMPLE_SMALL_KEY_MAX;
                break;
            case RG_SPIRIT_TEMPLE_MAP:
            case RG_SPIRIT_TEMPLE_COMPASS:
            case RG_SPIRIT_TEMPLE_SMALL_KEY:
            case RG_SPIRIT_TEMPLE_KEY_RING:
            case RG_SPIRIT_TEMPLE_BOSS_KEY:
                mapIndex = SCENE_SPIRIT_TEMPLE;
                numOfKeysOnKeyring = SPIRIT_TEMPLE_SMALL_KEY_MAX;
                break;
            case RG_SHADOW_TEMPLE_MAP:
            case RG_SHADOW_TEMPLE_COMPASS:
            case RG_SHADOW_TEMPLE_SMALL_KEY:
            case RG_SHADOW_TEMPLE_KEY_RING:
            case RG_SHADOW_TEMPLE_BOSS_KEY:
                mapIndex = SCENE_SHADOW_TEMPLE;
                numOfKeysOnKeyring = SHADOW_TEMPLE_SMALL_KEY_MAX;
                break;
            case RG_BOTTOM_OF_THE_WELL_MAP:
            case RG_BOTTOM_OF_THE_WELL_COMPASS:
            case RG_BOTTOM_OF_THE_WELL_SMALL_KEY:
            case RG_BOTTOM_OF_THE_WELL_KEY_RING:
                mapIndex = SCENE_BOTTOM_OF_THE_WELL;
                numOfKeysOnKeyring = BOTTOM_OF_THE_WELL_SMALL_KEY_MAX;
                break;
            case RG_ICE_CAVERN_MAP:
            case RG_ICE_CAVERN_COMPASS:
                mapIndex = SCENE_ICE_CAVERN;
                break;
            case RG_GANONS_CASTLE_BOSS_KEY:
                mapIndex = SCENE_GANONS_TOWER;
                break;
            case RG_GERUDO_TRAINING_GROUND_SMALL_KEY:
            case RG_GERUDO_TRAINING_GROUND_KEY_RING:
                mapIndex = SCENE_GERUDO_TRAINING_GROUND;
                numOfKeysOnKeyring = GERUDO_TRAINING_GROUND_SMALL_KEY_MAX;
                break;
            case RG_GERUDO_FORTRESS_SMALL_KEY:
            case RG_GERUDO_FORTRESS_KEY_RING:
                mapIndex = SCENE_THIEVES_HIDEOUT;
                numOfKeysOnKeyring = GERUDO_FORTRESS_SMALL_KEY_MAX;
                break;
            case RG_GANONS_CASTLE_SMALL_KEY:
            case RG_GANONS_CASTLE_KEY_RING:
                mapIndex = SCENE_INSIDE_GANONS_CASTLE;
                numOfKeysOnKeyring = GANONS_CASTLE_SMALL_KEY_MAX;
                break;
            default:
                break;
        }

        if ((item >= RG_FOREST_TEMPLE_SMALL_KEY) && (item <= RG_GANONS_CASTLE_SMALL_KEY)) {
            gSaveContext.ship.stats.dungeonKeys[mapIndex]++;
            if (gSaveContext.inventory.dungeonKeys[mapIndex] < 0) {
                gSaveContext.inventory.dungeonKeys[mapIndex] = 1;
            } else {
                gSaveContext.inventory.dungeonKeys[mapIndex]++;
            }
            return Return_Item_Entry(giEntry, RG_NONE);
        }

        if ((item >= RG_FOREST_TEMPLE_KEY_RING) && (item <= RG_GANONS_CASTLE_KEY_RING)) {
            gSaveContext.ship.stats.dungeonKeys[mapIndex] = numOfKeysOnKeyring;
            gSaveContext.inventory.dungeonKeys[mapIndex] = numOfKeysOnKeyring;
            return Return_Item_Entry(giEntry, RG_NONE);
        }

        u32 bitmask;
        if ((item >= RG_DEKU_TREE_MAP) && (item <= RG_ICE_CAVERN_MAP)) {
            bitmask = gBitFlags[2];
        } else if ((item >= RG_DEKU_TREE_COMPASS) && (item <= RG_ICE_CAVERN_COMPASS)) {
            bitmask = gBitFlags[1];
        } else {
            bitmask = gBitFlags[0];
        }

        gSaveContext.inventory.dungeonItems[mapIndex] |= bitmask;
        return Return_Item_Entry(giEntry, RG_NONE);
    } else if (item == RG_SKELETON_KEY) {
        Flags_SetRandomizerInf(RAND_INF_HAS_SKELETON_KEY);
        // This isn't technically necessary, because keys will no longer be consumed,
        // but for the player's sanity we display that they _have_ keys.
        gSaveContext.inventory.dungeonKeys[SCENE_FOREST_TEMPLE] = FOREST_TEMPLE_SMALL_KEY_MAX;
        gSaveContext.inventory.dungeonKeys[SCENE_FIRE_TEMPLE] = FIRE_TEMPLE_SMALL_KEY_MAX;
        gSaveContext.inventory.dungeonKeys[SCENE_WATER_TEMPLE] = WATER_TEMPLE_SMALL_KEY_MAX;
        gSaveContext.inventory.dungeonKeys[SCENE_SPIRIT_TEMPLE] = SPIRIT_TEMPLE_SMALL_KEY_MAX;
        gSaveContext.inventory.dungeonKeys[SCENE_SHADOW_TEMPLE] = SHADOW_TEMPLE_SMALL_KEY_MAX;
        gSaveContext.inventory.dungeonKeys[SCENE_BOTTOM_OF_THE_WELL] = BOTTOM_OF_THE_WELL_SMALL_KEY_MAX;
        gSaveContext.inventory.dungeonKeys[SCENE_GERUDO_TRAINING_GROUND] = GERUDO_TRAINING_GROUND_SMALL_KEY_MAX;
        gSaveContext.inventory.dungeonKeys[SCENE_THIEVES_HIDEOUT] = GERUDO_FORTRESS_SMALL_KEY_MAX;
        gSaveContext.inventory.dungeonKeys[SCENE_INSIDE_GANONS_CASTLE] = GANONS_CASTLE_SMALL_KEY_MAX;

        return Return_Item_Entry(giEntry, RG_NONE);
    } else if (item >= RG_GUARD_HOUSE_KEY && item <= RG_FISHING_HOLE_KEY) {
        Flags_SetRandomizerInf(
            (RandomizerInf)((int)RAND_INF_GUARD_HOUSE_UNLOCKED + ((item - RG_GUARD_HOUSE_KEY) * 2) + 1));
        return Return_Item_Entry(giEntry, RG_NONE);
    } else if (item >= RG_KEATON_MASK && item <= RG_MASK_OF_TRUTH) {
        Flags_SetRandomizerInf((RandomizerInf)((int)RAND_INF_CHILD_TRADES_HAS_MASK_KEATON + (item - RG_KEATON_MASK)));
        if (INV_CONTENT(ITEM_TRADE_CHILD) == ITEM_NONE) {
            INV_CONTENT(ITEM_TRADE_CHILD) = (int)ITEM_MASK_KEATON + (item - RG_KEATON_MASK);
        }
        return Return_Item_Entry(giEntry, RG_NONE);
    }

    switch (item) {
        case RG_MAGIC_SINGLE:
            gSaveContext.isMagicAcquired = true;
            gSaveContext.magicFillTarget = MAGIC_NORMAL_METER;
            Magic_Fill(play);
            break;
        case RG_MAGIC_DOUBLE:
            if (!gSaveContext.isMagicAcquired) {
                gSaveContext.isMagicAcquired = true;
            }
            gSaveContext.isDoubleMagicAcquired = true;
            gSaveContext.magicFillTarget = MAGIC_DOUBLE_METER;
            gSaveContext.magicLevel = 0;
            Magic_Fill(play);
            break;
        case RG_MAGIC_BEAN_PACK:
            if (INV_CONTENT(ITEM_BEAN) == ITEM_NONE) {
                INV_CONTENT(ITEM_BEAN) = ITEM_BEAN;
                AMMO(ITEM_BEAN) = 10;
            }
            break;
        case RG_DOUBLE_DEFENSE:
            gSaveContext.isDoubleDefenseAcquired = true;
            gSaveContext.inventory.defenseHearts = 20;
            gSaveContext.healthAccumulator = MAX_HEALTH;
            break;
        case RG_TYCOON_WALLET:
            Inventory_ChangeUpgrade(UPG_WALLET, 3);
            if (OTRGlobals::Instance->gRandomizer->GetRandoSettingValue(RSK_FULL_WALLETS)) {
                Rupees_ChangeBy(999);
            }
            break;
        case RG_CHILD_WALLET:
            Flags_SetRandomizerInf(RAND_INF_HAS_WALLET);
            if (OTRGlobals::Instance->gRandomizer->GetRandoSettingValue(RSK_FULL_WALLETS)) {
                Rupees_ChangeBy(99);
            }
            break;
        case RG_GREG_RUPEE:
            Rupees_ChangeBy(1);
            Flags_SetRandomizerInf(RAND_INF_GREG_FOUND);
            gSaveContext.ship.stats.itemTimestamp[TIMESTAMP_FOUND_GREG] = static_cast<u32>(GAMEPLAYSTAT_TOTAL_TIME);
            break;
        case RG_TRIFORCE_PIECE:
            gSaveContext.ship.quest.data.randomizer.triforcePiecesCollected++;
            GameInteractor_SetTriforceHuntPieceGiven(true);

            // Give Ganon's Boss Key and teleport to credits if set to Win when goal is reached.
            if (gSaveContext.ship.quest.data.randomizer.triforcePiecesCollected ==
                (OTRGlobals::Instance->gRandomizer->GetRandoSettingValue(RSK_TRIFORCE_HUNT_PIECES_REQUIRED) + 1)) {
                Flags_SetRandomizerInf(RAND_INF_GRANT_GANONS_BOSSKEY);

                if (OTRGlobals::Instance->gRandomizer->GetRandoSettingValue(RSK_TRIFORCE_HUNT) ==
                    RO_TRIFORCE_HUNT_WIN) {
                    // Save and warp are deferred until item queue drains
                    GameInteractor_SetTriforceHuntCreditsWarpActive(true);
                }
            }

            break;
        case RG_PROGRESSIVE_BOMBCHU_BAG:
            OTRGlobals::Instance->gRandoContext->HandleGetBombchuBag();
            break;
        case RG_MASTER_SWORD:
            if (!CHECK_OWNED_EQUIP(EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_MASTER)) {
                gSaveContext.inventory.equipment |= gBitFlags[1] << gEquipShifts[EQUIP_TYPE_SWORD];
            }
            break;
        case RG_DEKU_STICK_BAG:
            Inventory_ChangeUpgrade(UPG_STICKS, 1);
            INV_CONTENT(ITEM_STICK) = ITEM_STICK;
            AMMO(ITEM_STICK) = static_cast<int8_t>(CUR_CAPACITY(UPG_STICKS));
            break;
        case RG_DEKU_NUT_BAG:
            Inventory_ChangeUpgrade(UPG_NUTS, 1);
            INV_CONTENT(ITEM_NUT) = ITEM_NUT;
            AMMO(ITEM_NUT) = static_cast<int8_t>(CUR_CAPACITY(UPG_NUTS));
            break;
        // Custom Items (Second Inventory Page)
        // IMPORTANT: Use ExtInv_SetItemById() instead of INV_CONTENT() for custom items
        // to avoid buffer overflow on gItemSlots[] array (which only has 54 elements)
        case RG_ROCS_FEATHER:
            // Vanilla rando Roc's Feather: lives in the Nayru's Love slot and cycles with it
            // (see RocsFeatherCycle.c). Skijer's feather is RG_PROGRESSIVE_ROCS instead.
            Flags_SetRandomizerInf(RAND_INF_OBTAINED_ROCS_FEATHER);
            if (INV_CONTENT(ITEM_NAYRUS_LOVE) == ITEM_NONE) {
                INV_CONTENT(ITEM_NAYRUS_LOVE) = ITEM_ROCS_FEATHER;
            }
            break;
        case RG_ROCS_CAPE:
            ExtInv_SetItemById(ITEM_ROCS_CAPE);
            break;
        case RG_PROGRESSIVE_ROCS:
            // Progressive Roc's: Give Feather first, then Cape as upgrade
            switch (gSaveContext.inventory.items[SLOT_ROCS]) {
                case ITEM_NONE:
                    ExtInv_SetItemById(ITEM_ROCS_FEATHER_SKIJER);
                    break;
                case ITEM_ROCS_FEATHER_SKIJER:
                default:
                    ExtInv_SetItemById(ITEM_ROCS_CAPE);
                    break;
            }
            break;
        case RG_WHIP:
            ExtInv_SetItemById(ITEM_WHIP);
            break;
        case RG_SPINNER:
            ExtInv_SetItemById(ITEM_SPINNER);
            break;
        case RG_BOMB_ARROWS:
            ExtInv_SetItemById(ITEM_BOMB_ARROWS);
            break;
        case RG_FIRE_ROD:
            ExtInv_SetItemById(ITEM_ROD_FIRE);
            break;
        case RG_DEMISE_DESTRUCTION:
            ExtInv_SetItemById(ITEM_DEMISE_DESTRUCTION);
            break;
        case RG_DEKU_LEAF:
            ExtInv_SetItemById(ITEM_DEKU_LEAF);
            break;
        case RG_TIME_GATE:
            ExtInv_SetItemById(ITEM_TIME_GATE);
            break;
        case RG_BEETLE:
            ExtInv_SetItemById(ITEM_BEETLE);
            break;
        case RG_DESIRE_SENSOR:
            ExtInv_SetItemById(ITEM_DESIRE_SENSOR);
            break;
        case RG_SWITCH_HOOK:
            ExtInv_SetItemById(ITEM_SWITCH_HOOK);
            break;
        case RG_ICE_ROD:
            ExtInv_SetItemById(ITEM_ROD_ICE);
            break;
        case RG_ZONAI_PERMAFROST:
            ExtInv_SetItemById(ITEM_ZONAI_PERMAFROST);
            break;
        case RG_MOGMA_MITTS:
            ExtInv_SetItemById(ITEM_MOGMA_MITTS);
            break;
        case RG_GUST_JAR:
            ExtInv_SetItemById(ITEM_GUST_JAR);
            break;
        case RG_BALL_AND_CHAIN:
            ExtInv_SetItemById(ITEM_BALL_AND_CHAIN);
            break;
        case RG_LIGHT_ROD:
            ExtInv_SetItemById(ITEM_ROD_LIGHT);
            break;
        case RG_HYLIAS_GRACE:
            ExtInv_SetItemById(ITEM_HYLIAS_GRACE);
            break;
        case RG_LANTERN:
            ExtInv_SetItemById(ITEM_LANTERN);
            break;
        case RG_PENDING_1:
            ExtInv_SetItemById(ITEM_MINISH_CAP);
            break;
        case RG_PENDING_3:
            ExtInv_SetItemById(ITEM_POKEBALL);
            break;
        case RG_CANE_OF_SOMARIA:
            ExtInv_SetItemById(ITEM_CANE_OF_SOMARIA);
            break;
        case RG_SHOVEL:
            ExtInv_SetItemById(ITEM_SHOVEL);
            break;
        case RG_DOMINION_ROD:
            ExtInv_SetItemById(ITEM_DOMINION_ROD);
            break;
        // Extended Equipment (ownership bits in upper 16 of inventory.equipment)
        case RG_EXT_CANE_OF_BYRNA:
            ExtEquip_GiveItem(EQUIP_TYPE_SWORD, 1);
            break;
        case RG_EXT_FOUR_SWORD:
            ExtEquip_GiveItem(EQUIP_TYPE_SWORD, 2);
            break;
        case RG_EXT_IRON_KNUCKLE_AXE:
            ExtEquip_GiveItem(EQUIP_TYPE_SWORD, 3);
            break;
        case RG_EXT_DIVINE_SHIELD:
            ExtEquip_GiveItem(EQUIP_TYPE_SHIELD, 1);
            break;
        case RG_EXT_SHEIKAH_SHIELD:
            ExtEquip_GiveItem(EQUIP_TYPE_SHIELD, 2);
            break;
        case RG_EXT_SHIELD_OF_IKANA:
            ExtEquip_GiveItem(EQUIP_TYPE_SHIELD, 3);
            break;
        case RG_EXT_MAGIC_CAPE:
            ExtEquip_GiveItem(EQUIP_TYPE_TUNIC, 1);
            break;
        case RG_EXT_SPIRIT_BREASTPLATE:
            ExtEquip_GiveItem(EQUIP_TYPE_TUNIC, 2);
            break;
        case RG_EXT_CHAMPIONS_TUNIC:
            ExtEquip_GiveItem(EQUIP_TYPE_TUNIC, 3);
            break;
        case RG_EXT_PEGASUS_ANKLET:
            ExtEquip_GiveItem(EQUIP_TYPE_BOOTS, 1);
            break;
        case RG_EXT_PENDANT_OF_MEMORIES:
            ExtEquip_GiveItem(EQUIP_TYPE_BOOTS, 2);
            break;
        case RG_EXT_WATER_DRAGON_SCALE:
            ExtEquip_GiveItem(EQUIP_TYPE_BOOTS, 3);
            break;
        // MM Masks (Third Inventory Page)
        case RG_MM_MASK_POSTMAN:
            ExtInv_SetItemById(ITEM_MM_MASK_POSTMAN);
            break;
        case RG_MM_MASK_ALL_NIGHT:
            ExtInv_SetItemById(ITEM_MM_MASK_ALL_NIGHT);
            break;
        case RG_MM_MASK_BLAST:
            ExtInv_SetItemById(ITEM_MM_MASK_BLAST);
            break;
        case RG_MM_MASK_STONE:
            ExtInv_SetItemById(ITEM_MM_MASK_STONE);
            break;
        case RG_MM_MASK_GREAT_FAIRY:
            ExtInv_SetItemById(ITEM_MM_MASK_GREAT_FAIRY);
            break;
        case RG_MM_MASK_DEKU:
            ExtInv_SetItemById(ITEM_MM_MASK_DEKU);
            break;
        case RG_MM_MASK_KEATON:
            ExtInv_SetItemById(ITEM_MM_MASK_KEATON);
            // Also give OOT Keaton Mask so trade quest interactions work (gate guard, etc.)
            Flags_SetRandomizerInf(RAND_INF_CHILD_TRADES_HAS_MASK_KEATON);
            if (INV_CONTENT(ITEM_TRADE_CHILD) == ITEM_NONE) {
                INV_CONTENT(ITEM_TRADE_CHILD) = ITEM_MASK_KEATON;
            }
            break;
        case RG_MM_MASK_BREMEN:
            ExtInv_SetItemById(ITEM_MM_MASK_BREMEN);
            break;
        case RG_MM_MASK_BUNNY:
            ExtInv_SetItemById(ITEM_MM_MASK_BUNNY);
            // Also give OOT Bunny Hood so vanilla equip effect works
            Flags_SetRandomizerInf(RAND_INF_CHILD_TRADES_HAS_MASK_BUNNY);
            if (INV_CONTENT(ITEM_TRADE_CHILD) == ITEM_NONE) {
                INV_CONTENT(ITEM_TRADE_CHILD) = ITEM_MASK_BUNNY;
            }
            break;
        case RG_MM_MASK_DON_GERO:
            ExtInv_SetItemById(ITEM_MM_MASK_DON_GERO);
            break;
        case RG_MM_MASK_SCENTS:
            ExtInv_SetItemById(ITEM_MM_MASK_SCENTS);
            break;
        case RG_MM_MASK_GORON:
            ExtInv_SetItemById(ITEM_MM_MASK_GORON);
            // Also give OOT Goron Mask so trade quest interactions work
            Flags_SetRandomizerInf(RAND_INF_CHILD_TRADES_HAS_MASK_GORON);
            if (INV_CONTENT(ITEM_TRADE_CHILD) == ITEM_NONE) {
                INV_CONTENT(ITEM_TRADE_CHILD) = ITEM_MASK_GORON;
            }
            break;
        case RG_MM_MASK_ROMANI:
            ExtInv_SetItemById(ITEM_MM_MASK_ROMANI);
            break;
        case RG_MM_MASK_CIRCUS_LEADER:
            ExtInv_SetItemById(ITEM_MM_MASK_CIRCUS_LEADER);
            break;
        case RG_MM_MASK_KAFEI:
            ExtInv_SetItemById(ITEM_MM_MASK_KAFEI);
            break;
        case RG_MM_MASK_COUPLE:
            ExtInv_SetItemById(ITEM_MM_MASK_COUPLE);
            break;
        case RG_MM_MASK_TRUTH:
            ExtInv_SetItemById(ITEM_MM_MASK_TRUTH);
            // Also give OOT Mask of Truth so vanilla equip effect works (Gossip Stones, etc.)
            Flags_SetRandomizerInf(RAND_INF_CHILD_TRADES_HAS_MASK_TRUTH);
            if (INV_CONTENT(ITEM_TRADE_CHILD) == ITEM_NONE) {
                INV_CONTENT(ITEM_TRADE_CHILD) = ITEM_MASK_TRUTH;
            }
            break;
        case RG_MM_MASK_ZORA:
            ExtInv_SetItemById(ITEM_MM_MASK_ZORA);
            // Also give OOT Zora Mask so trade quest interactions work
            Flags_SetRandomizerInf(RAND_INF_CHILD_TRADES_HAS_MASK_ZORA);
            if (INV_CONTENT(ITEM_TRADE_CHILD) == ITEM_NONE) {
                INV_CONTENT(ITEM_TRADE_CHILD) = ITEM_MASK_ZORA;
            }
            break;
        case RG_MM_MASK_KAMARO:
            ExtInv_SetItemById(ITEM_MM_MASK_KAMARO);
            break;
        case RG_MM_MASK_GIBDO:
            ExtInv_SetItemById(ITEM_MM_MASK_GIBDO);
            break;
        case RG_MM_MASK_GARO:
            ExtInv_SetItemById(ITEM_MM_MASK_GARO);
            break;
        case RG_MM_MASK_CAPTAIN:
            ExtInv_SetItemById(ITEM_MM_MASK_CAPTAIN);
            break;
        case RG_MM_MASK_GIANT:
            ExtInv_SetItemById(ITEM_MM_MASK_GIANT);
            break;
        case RG_MM_MASK_FIERCE_DEITY:
            ExtInv_SetItemById(ITEM_MM_MASK_FIERCE_DEITY);
            break;
        default:
            LUSLOG_WARN("Randomizer_Item_Give didn't have behaviour specified for getItemId=%d", item);
            assert(false);
            return -1;
    }

    return Return_Item_Entry(giEntry, RG_NONE);
}
