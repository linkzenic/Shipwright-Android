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
    { RG_ROCS_FEATHER, static_cast<ItemID>(ITEM_ROCS_FEATHER_SKIJER),
      "You got %rRoc's Feather%w!&This magical feather lets you&jump higher than normal.^Assign it to %y\xA1%w and "
      "press&to perform a high jump.&It even works in water!",
      "Du hast %rRocs Feder%w erhalten!&Diese magische Feder lässt&dich höher springen.^Weise sie %y\xA1%w zu und "
      "drücke&um hoch zu springen.&Funktioniert auch im Wasser!",
      "Vous obtenez la %rPlume de Roc%w!&Cette plume magique vous&permet de sauter plus haut.^Assignez-la à %y\xA1%w et "
      "appuyez&pour faire un grand saut.&Fonctionne même dans l'eau!" },

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
      "You got the %yPostman's Hat%w!&A mask from Majora's Mask.",
      "Du hast den %yBriefträgerhut%w!&Eine Maske aus Majoras Mask.",
      "Vous obtenez le %yChapeau du Facteur%w!&Un masque de Majora's Mask." },
    { RG_MM_MASK_ALL_NIGHT, static_cast<ItemID>(ITEM_MM_MASK_ALL_NIGHT),
      "You got the %yAll-Night Mask%w!&A mask from Majora's Mask.",
      "Du hast die %yNachtmaske%w!&Eine Maske aus Majoras Mask.",
      "Vous obtenez le %yMasque de Nuit%w!&Un masque de Majora's Mask." },
    { RG_MM_MASK_BLAST, static_cast<ItemID>(ITEM_MM_MASK_BLAST),
      "You got the %yBlast Mask%w!&A mask from Majora's Mask.",
      "Du hast die %yExplosionsmaske%w!&Eine Maske aus Majoras Mask.",
      "Vous obtenez le %yMasque d'Explosion%w!&Un masque de Majora's Mask." },
    { RG_MM_MASK_STONE, static_cast<ItemID>(ITEM_MM_MASK_STONE),
      "You got the %yStone Mask%w!&A mask from Majora's Mask.",
      "Du hast die %ySteinmaske%w!&Eine Maske aus Majoras Mask.",
      "Vous obtenez le %yMasque de Pierre%w!&Un masque de Majora's Mask." },
    { RG_MM_MASK_GREAT_FAIRY, static_cast<ItemID>(ITEM_MM_MASK_GREAT_FAIRY),
      "You got the %yGreat Fairy Mask%w!&A mask from Majora's Mask.",
      "Du hast die %yFeenmaske%w!&Eine Maske aus Majoras Mask.",
      "Vous obtenez le %yMasque de la Grande Fée%w!&Un masque de Majora's Mask." },
    { RG_MM_MASK_DEKU, static_cast<ItemID>(ITEM_MM_MASK_DEKU),
      "You got the %gDeku Mask%w!&A transformation mask from&Majora's Mask.",
      "Du hast die %gDeku-Maske%w!&Eine Verwandlungsmaske aus&Majoras Mask.",
      "Vous obtenez le %gMasque Mojo%w!&Un masque de transformation&de Majora's Mask." },
    { RG_MM_MASK_KEATON, static_cast<ItemID>(ITEM_MM_MASK_KEATON),
      "You got the %yKeaton Mask%w!&A mask from Majora's Mask.",
      "Du hast die %yKeaton-Maske%w!&Eine Maske aus Majoras Mask.",
      "Vous obtenez le %yMasque de Keaton%w!&Un masque de Majora's Mask." },
    { RG_MM_MASK_BREMEN, static_cast<ItemID>(ITEM_MM_MASK_BREMEN),
      "You got the %yBremen Mask%w!&A mask from Majora's Mask.",
      "Du hast die %yBremen-Maske%w!&Eine Maske aus Majoras Mask.",
      "Vous obtenez le %yMasque de Brême%w!&Un masque de Majora's Mask." },
    { RG_MM_MASK_BUNNY, static_cast<ItemID>(ITEM_MM_MASK_BUNNY),
      "You got the %yBunny Hood%w!&A mask from Majora's Mask.",
      "Du hast die %yHasenohren%w!&Eine Maske aus Majoras Mask.",
      "Vous obtenez le %yMasque de Lapin%w!&Un masque de Majora's Mask." },
    { RG_MM_MASK_DON_GERO, static_cast<ItemID>(ITEM_MM_MASK_DON_GERO),
      "You got %yDon Gero's Mask%w!&A mask from Majora's Mask.",
      "Du hast %yDon Geros Maske%w!&Eine Maske aus Majoras Mask.",
      "Vous obtenez le %yMasque de Don Gero%w!&Un masque de Majora's Mask." },
    { RG_MM_MASK_SCENTS, static_cast<ItemID>(ITEM_MM_MASK_SCENTS),
      "You got the %yMask of Scents%w!&A mask from Majora's Mask.",
      "Du hast die %yGeruchsmaske%w!&Eine Maske aus Majoras Mask.",
      "Vous obtenez le %yMasque des Odeurs%w!&Un masque de Majora's Mask." },
    { RG_MM_MASK_GORON, static_cast<ItemID>(ITEM_MM_MASK_GORON),
      "You got the %rGoron Mask%w!&A transformation mask from&Majora's Mask.",
      "Du hast die %rGoronen-Maske%w!&Eine Verwandlungsmaske aus&Majoras Mask.",
      "Vous obtenez le %rMasque de Goron%w!&Un masque de transformation&de Majora's Mask." },
    { RG_MM_MASK_ROMANI, static_cast<ItemID>(ITEM_MM_MASK_ROMANI),
      "You got %yRomani's Mask%w!&A mask from Majora's Mask.",
      "Du hast %yRomanis Maske%w!&Eine Maske aus Majoras Mask.",
      "Vous obtenez le %yMasque de Romani%w!&Un masque de Majora's Mask." },
    { RG_MM_MASK_CIRCUS_LEADER, static_cast<ItemID>(ITEM_MM_MASK_CIRCUS_LEADER),
      "You got the %yCircus Leader's Mask%w!&A mask from Majora's Mask.",
      "Du hast die %yZirkusleitermaske%w!&Eine Maske aus Majoras Mask.",
      "Vous obtenez le %yMasque du Chef de Cirque%w!&Un masque de Majora's Mask." },
    { RG_MM_MASK_KAFEI, static_cast<ItemID>(ITEM_MM_MASK_KAFEI), "You got %yKafei's Mask%w!&A mask from Majora's Mask.",
      "Du hast %yKafeis Maske%w!&Eine Maske aus Majoras Mask.",
      "Vous obtenez le %yMasque de Kafei%w!&Un masque de Majora's Mask." },
    { RG_MM_MASK_COUPLE, static_cast<ItemID>(ITEM_MM_MASK_COUPLE),
      "You got the %yCouple's Mask%w!&A mask from Majora's Mask.",
      "Du hast die %yPaarmaske%w!&Eine Maske aus Majoras Mask.",
      "Vous obtenez le %yMasque des Amoureux%w!&Un masque de Majora's Mask." },
    { RG_MM_MASK_TRUTH, static_cast<ItemID>(ITEM_MM_MASK_TRUTH),
      "You got the %yMask of Truth%w!&A mask from Majora's Mask.",
      "Du hast die %yMaske der Wahrheit%w!&Eine Maske aus Majoras Mask.",
      "Vous obtenez le %yMasque de Vérité%w!&Un masque de Majora's Mask." },
    { RG_MM_MASK_ZORA, static_cast<ItemID>(ITEM_MM_MASK_ZORA),
      "You got the %bZora Mask%w!&A transformation mask from&Majora's Mask.",
      "Du hast die %bZora-Maske%w!&Eine Verwandlungsmaske aus&Majoras Mask.",
      "Vous obtenez le %bMasque de Zora%w!&Un masque de transformation&de Majora's Mask." },
    { RG_MM_MASK_KAMARO, static_cast<ItemID>(ITEM_MM_MASK_KAMARO),
      "You got %yKamaro's Mask%w!&A mask from Majora's Mask.",
      "Du hast %yKamaros Maske%w!&Eine Maske aus Majoras Mask.",
      "Vous obtenez le %yMasque de Kamaro%w!&Un masque de Majora's Mask." },
    { RG_MM_MASK_GIBDO, static_cast<ItemID>(ITEM_MM_MASK_GIBDO),
      "You got the %yGibdo Mask%w!&A mask from Majora's Mask.",
      "Du hast die %yGibdo-Maske%w!&Eine Maske aus Majoras Mask.",
      "Vous obtenez le %yMasque de Gibdo%w!&Un masque de Majora's Mask." },
    { RG_MM_MASK_GARO, static_cast<ItemID>(ITEM_MM_MASK_GARO),
      "You got the %yGaro's Mask%w!&A mask from Majora's Mask.",
      "Du hast %yGaros Maske%w!&Eine Maske aus Majoras Mask.",
      "Vous obtenez le %yMasque de Garo%w!&Un masque de Majora's Mask." },
    { RG_MM_MASK_CAPTAIN, static_cast<ItemID>(ITEM_MM_MASK_CAPTAIN),
      "You got the %yCaptain's Hat%w!&A mask from Majora's Mask.",
      "Du hast den %yKapitänshut%w!&Eine Maske aus Majoras Mask.",
      "Vous obtenez la %yCasquette du Capitaine%w!&Un masque de Majora's Mask." },
    { RG_MM_MASK_GIANT, static_cast<ItemID>(ITEM_MM_MASK_GIANT),
      "You got the %yGiant's Mask%w!&A mask from Majora's Mask.",
      "Du hast die %yRiesenmaske%w!&Eine Maske aus Majoras Mask.",
      "Vous obtenez le %yMasque de Géant%w!&Un masque de Majora's Mask." },
    { RG_MM_MASK_FIERCE_DEITY, static_cast<ItemID>(ITEM_MM_MASK_FIERCE_DEITY),
      "You got the %pFierce Deity Mask%w!&A transformation mask from&Majora's Mask.",
      "Du hast %pMajoras Maske%w!&Eine Verwandlungsmaske aus&Majoras Mask.",
      "Vous obtenez le %pMasque du Dieu Féroce%w!&Un masque de transformation&de Majora's Mask." },

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
    // Extended Equipment (12 items, equipment page 2 — toggled via [L] in pause)
    // ─────────────────────────────────────────────────────────────────────────
    { RG_EXT_CANE_OF_BYRNA, static_cast<ItemID>(ITEM_EXT_SWORD_1),
      "You got the %cCane of Byrna%w!&A magical sword that wraps you&in a protective shield.^Equip on the %ysword slot%w&from the equipment page.&Press %y\xA2%w to toggle pages.^While equipped, your %gMagic%w&drains slowly but you become&%cinvincible%w to all damage!&Sword hits restore Magic.",
      "Du hast den %cStab von Byrna%w!&Ein magisches Schwert das dich&in einen Schutzschild hüllt.^Rüste ihn am %ySchwert-Platz%w&aus der Ausrüstungsseite aus.&Drücke %y\xA2%w zum Wechseln.^Während ausgerüstet, leert sich&deine %gMagie%w langsam, aber du&wirst %cunverwundbar%w!&Schwerthiebe stellen Magie wieder her.",
      "Vous obtenez la %cCanne de Byrna%w!&Une épée magique qui vous&entoure d'un bouclier.^Équipez-la dans l'%yemplacement épée%w&depuis la page équipement.&Appuyez sur %y\xA2%w pour changer.^Tant qu'équipée, votre %gMagie%w&se vide lentement mais vous&êtes %cinvincible%w!&Les coups restaurent la Magie." },

    { RG_EXT_FOUR_SWORD, static_cast<ItemID>(ITEM_EXT_SWORD_2),
      "You got the %gFour Sword%w!&A blade said to split its wielder&into four heroes.^Equip on the %ysword slot%w from&the equipment page (%y\xA2%w to toggle).^While equipped, %g3 ghost clones%w&of Link follow you and mirror&your sword swings.&Surround your foes!",
      "Du hast das %gVier-Schwert%w!&Eine Klinge die ihren Träger&in vier Helden teilen soll.^Rüste es am %ySchwert-Platz%w aus&(%y\xA2%w zum Wechseln).^Während ausgerüstet folgen dir&%g3 Geist-Klone%w von Link und&spiegeln deine Schwerthiebe.&Umzingle deine Feinde!",
      "Vous obtenez l'%gÉpée de Quatre%w!&Une lame dont on dit qu'elle&divise son porteur en quatre héros.^Équipez-la dans l'%yemplacement épée%w&(%y\xA2%w pour changer).^Tant qu'équipée, %g3 clones spectraux%w&de Link vous suivent et imitent&vos attaques. Encerclez l'ennemi!" },

    { RG_EXT_IRON_KNUCKLE_AXE, static_cast<ItemID>(ITEM_EXT_SWORD_3),
      "You got the %rIron Knuckle Axe%w!&A massive tomahawk wielded by&the armored knights of Ganon.^Equip on the %ysword slot%w (%y\xA2%w&toggles pages on equipment screen).^Press %y\xA0%w to swing the heavy axe.&Hold %y\xA0%w to charge a %rthrow%w —&the axe boomerangs back to you!^Crushes shields and breaks rocks.",
      "Du hast die %rEisenknöchel-Axt%w!&Ein massiver Tomahawk der&gepanzerten Ritter Ganons.^Rüste sie am %ySchwert-Platz%w aus&(%y\xA2%w wechselt die Seite).^%y\xA0%w schwingt die schwere Axt.&Halte %y\xA0%w für einen %rWurf%w —&die Axt kommt zurück!^Zerschmettert Schilde und Felsen.",
      "Vous obtenez la %rHache d'Iron Knuckle%w!&Un tomahawk massif des chevaliers&en armure de Ganon.^Équipez-la dans l'%yemplacement épée%w&(%y\xA2%w change de page).^%y\xA0%w pour swinguer la lourde hache.&Maintenez %y\xA0%w pour charger un %rlancer%w —&la hache revient à vous!^Brise les boucliers et les rochers." },

    { RG_EXT_DIVINE_SHIELD, static_cast<ItemID>(ITEM_EXT_SHIELD_1),
      "You got the %yDivine Shield%w!&A blessed shield said to repel&even the darkest of magics.^Equip on the %yshield slot%w&from the equipment page.&Press %y\xA2%w to toggle pages.^While equipped, hold %y\xA3%w to&block. The shield reflects&%cmagical projectiles%w back at&their casters!",
      "Du hast den %yGötterschild%w!&Ein gesegneter Schild der selbst&dunkelste Magie abwehren soll.^Rüste ihn am %ySchild-Platz%w aus&(%y\xA2%w zum Wechseln).^Halte %y\xA3%w zum Blocken.&Der Schild reflektiert&%cmagische Geschosse%w zurück&zu ihren Werfern!",
      "Vous obtenez le %yBouclier Divin%w!&Un bouclier béni qui repousse&même la magie la plus sombre.^Équipez-le dans l'%yemplacement bouclier%w&(%y\xA2%w pour changer).^Maintenez %y\xA3%w pour parer.&Il renvoie les %cprojectiles magiques%w&vers leurs lanceurs!" },

    { RG_EXT_SHEIKAH_SHIELD, static_cast<ItemID>(ITEM_EXT_SHIELD_2),
      "You got the %cKite Shield%w!&A light, agile shield favored by&the Sheikah warriors.^Equip on the %yshield slot%w&(%y\xA2%w toggles equipment pages).^Lighter than other shields —&you can %gdash%w and %gjump%w more&freely while it's equipped.&Hold %y\xA3%w to block normally.",
      "Du hast den %cDrachenschild%w!&Ein leichter, beweglicher Schild&den Sheikah-Krieger bevorzugen.^Rüste ihn am %ySchild-Platz%w aus&(%y\xA2%w wechselt Seiten).^Leichter als andere Schilde —&du kannst dich freier %gbewegen%w&und %ghöher springen%w!&%y\xA3%w zum Blocken.",
      "Vous obtenez le %cBouclier Cerf-Volant%w!&Un bouclier léger et agile&prisé par les guerriers Sheikah.^Équipez-le dans l'%yemplacement bouclier%w&(%y\xA2%w change de page).^Plus léger que les autres —&vous pouvez %gcourir%w et %gsauter%w&plus librement! %y\xA3%w pour parer." },

    { RG_EXT_SHIELD_OF_IKANA, static_cast<ItemID>(ITEM_EXT_SHIELD_3),
      "You got the %pShield of Ikana%w!&A cursed mirror shield from the&fallen Ikana Kingdom.^Equip on the %yshield slot%w&(%y\xA2%w toggles equipment pages).^%cPerfect Guard%w (block within&12 frames of attack): drain&enemy %rHP%w and heal yourself!^%pDeath Save%w: revive once per&life with darkness aura when&fatally struck.",
      "Du hast den %pSchild von Ikana%w!&Ein verfluchter Spiegelschild aus&dem gefallenen Königreich Ikana.^Rüste ihn am %ySchild-Platz%w aus&(%y\xA2%w wechselt Seiten).^%cPerfekter Block%w (innerhalb 12&Frames blocken): saugt feindliche&%rHP%w und heilt dich!^%pTodesrettung%w: Einmal pro Leben&mit Dunkelheits-Aura wiederbelebt&bei tödlichem Treffer.",
      "Vous obtenez le %pBouclier d'Ikana%w!&Un bouclier-miroir maudit du&royaume déchu d'Ikana.^Équipez-le dans l'%yemplacement bouclier%w&(%y\xA2%w change de page).^%cParade Parfaite%w (bloquer en moins&de 12 frames): vole les %rPV%w&ennemis et vous soigne!^%pSauvegarde de Mort%w: ressuscite&une fois par vie avec une aura&sombre lors d'un coup fatal." },

    { RG_EXT_MAGIC_CAPE, static_cast<ItemID>(ITEM_EXT_TUNIC_1),
      "You got the %pMagic Cape%w!&Ganondorf's enchanted cloak,&with full cloth physics.^Equip on the %ytunic slot%w&from the equipment page.&Press %y\xA2%w to toggle pages.^While equipped, the cape flows&behind you with the wind.&You recover %ghalf the magic%w&you spend each frame!",
      "Du hast den %pZauberumhang%w!&Ganondorfs verzauberter Mantel,&mit voller Stoff-Physik.^Rüste ihn am %yTunika-Platz%w aus&(%y\xA2%w zum Wechseln).^Während ausgerüstet weht der&Umhang im Wind hinter dir.&Du erhältst die %gHälfte der Magie%w&die du verbrauchst zurück!",
      "Vous obtenez la %pCape Magique%w!&Le manteau enchanté de Ganondorf,&avec physique de tissu complète.^Équipez-la dans l'%yemplacement tunique%w&(%y\xA2%w pour changer).^Tant qu'équipée, la cape flotte&derrière vous au vent.&Vous récupérez la %gmoitié de la Magie%w&dépensée chaque frame!" },

    { RG_EXT_SPIRIT_BREASTPLATE, static_cast<ItemID>(ITEM_EXT_TUNIC_2),
      "You got the %yMagic Armor%w!&A golden breastplate that uses&Magic to absorb damage.^Equip on the %ytunic slot%w&(%y\xA2%w toggles equipment pages).^While equipped, %gMagic drains%w&each time you take damage&instead of your hearts!^If you run out of magic, the&armor fails until refilled.",
      "Du hast die %yMagische Rüstung%w!&Ein goldener Brustpanzer der&Magie nutzt um Schaden zu absorbieren.^Rüste sie am %yTunika-Platz%w aus&(%y\xA2%w wechselt Seiten).^Während ausgerüstet leert sich&%gdeine Magie%w bei Schaden&statt deiner Herzen!^Ohne Magie versagt die Rüstung,&bis sie wieder aufgefüllt ist.",
      "Vous obtenez l'%yArmure Magique%w!&Un plastron doré qui utilise&la Magie pour absorber les dégâts.^Équipez-la dans l'%yemplacement tunique%w&(%y\xA2%w change de page).^Tant qu'équipée, votre %gMagie%w&se vide à chaque dégât&au lieu de vos cœurs!^Sans Magie, l'armure cesse de&fonctionner jusqu'au rechargement." },

    { RG_EXT_CHAMPIONS_TUNIC, static_cast<ItemID>(ITEM_EXT_TUNIC_3),
      "You got the %cChampion's Tunic%w!&The blue garb of Hyrule's chosen&hero, blessed with battle aura.^Equip on the %ytunic slot%w&(%y\xA2%w toggles equipment pages).^A %cblue tint%w covers the screen.&Land sword hits in quick succession&to charge a %gFlurry Rush%w finisher!",
      "Du hast die %cRüstung des Helden%w!&Die blaue Tracht des auserwählten&Helden Hyrules, mit Kampfaura gesegnet.^Rüste sie am %yTunika-Platz%w aus&(%y\xA2%w wechselt Seiten).^Ein %cblauer Schimmer%w bedeckt&den Bildschirm. Lande schnelle&Schwerttreffer für %gFlurry Rush%w!",
      "Vous obtenez la %cTunique du Héros%w!&Le vêtement bleu du héros élu&d'Hyrule, béni d'une aura de combat.^Équipez-la dans l'%yemplacement tunique%w&(%y\xA2%w change de page).^Une %cteinte bleue%w recouvre l'écran.&Enchaînez les coups d'épée pour&charger un finisseur %gFlurry Rush%w!" },

    { RG_EXT_PEGASUS_ANKLET, static_cast<ItemID>(ITEM_EXT_BOOTS_1),
      "You got the %rPegasus Anklet%w!&Winged anklets that grant the&speed of the legendary Pegasus.^Equip on the %yboots slot%w&from the equipment page.&Press %y\xA2%w to toggle pages.^While equipped, you %gsprint%w&permanently and a %ywind barrier%w&deflects light projectiles!",
      "Du hast den %rPegasus-Fußreif%w!&Geflügelte Fußreifen mit der&Geschwindigkeit des Pegasus.^Rüste sie am %yStiefel-Platz%w aus&(%y\xA2%w zum Wechseln).^Während ausgerüstet rennst du&permanent und eine %yWindbarriere%w&lenkt leichte Geschosse ab!",
      "Vous obtenez le %rBracelet de Pégase%w!&Des bracelets ailés qui octroient&la vitesse du légendaire Pégase.^Équipez-le dans l'%yemplacement bottes%w&(%y\xA2%w pour changer).^Tant qu'équipé, vous %gsprintez%w&en permanence et une %ybarrière de vent%w&dévie les projectiles légers!" },

    { RG_EXT_PENDANT_OF_MEMORIES, static_cast<ItemID>(ITEM_EXT_BOOTS_2),
      "You got the %pPendant of Memories%w!&A keepsake that lets you walk&between past and present.^Equip on the %yboots slot%w&(%y\xA2%w toggles equipment pages).^While equipped, your footsteps&leave faint %pghost trails%w that&briefly mark where you stood.&Useful for solving puzzles!",
      "Du hast das %pAmulett der Erinnerungen%w!&Ein Andenken das dich zwischen&Vergangenheit und Gegenwart laufen lässt.^Rüste es am %yStiefel-Platz%w aus&(%y\xA2%w wechselt Seiten).^Während ausgerüstet hinterlassen&deine Schritte schwache %pGeisterspuren%w&die markieren wo du standest.",
      "Vous obtenez le %pPendentif des Souvenirs%w!&Un souvenir qui vous laisse marcher&entre passé et présent.^Équipez-le dans l'%yemplacement bottes%w&(%y\xA2%w change de page).^Tant qu'équipé, vos pas laissent&de faibles %ptraînées spectrales%w qui&marquent où vous étiez." },

    { RG_EXT_WATER_DRAGON_SCALE, static_cast<ItemID>(ITEM_EXT_BOOTS_3),
      "You got the %bWater Dragon Scale%w!&A blessed scale of the Water&Dragon, master of the depths.^Equip on the %yboots slot%w&(%y\xA2%w toggles equipment pages).^While equipped, you swim&%gfaster%w and a %bwater barrier%w&surrounds you underwater,&deflecting projectiles!",
      "Du hast die %bWasserdrachen-Schuppe%w!&Eine gesegnete Schuppe des&Wasserdrachen, Herrscher der Tiefen.^Rüste sie am %yStiefel-Platz%w aus&(%y\xA2%w wechselt Seiten).^Während ausgerüstet schwimmst du&%gschneller%w und eine %bWasserbarriere%w&umgibt dich unter Wasser!",
      "Vous obtenez l'%bÉcaille du Dragon d'Eau%w!&Une écaille bénie du Dragon&d'Eau, maître des profondeurs.^Équipez-la dans l'%yemplacement bottes%w&(%y\xA2%w change de page).^Tant qu'équipée, vous nagez&%gplus vite%w et une %bbarrière d'eau%w&vous entoure sous l'eau!" },
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
    } catch (std::exception& e) {}
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

    Ship::Context::GetInstance()->GetFileDropMgr()->RegisterDropHandler(Rando_HandleSpoilerDrop);

    // Fix texture cache coherency for file select menu
    // This ensures UI textures are properly loaded after randomizer initialization
    // to prevent pink glitches/static artifacts on the file select screen
    auto resMgr = Ship::Context::GetInstance()->GetResourceManager();
    if (resMgr != nullptr) {
        // Unload and reload critical UI textures to ensure cache coherency
        // These textures are used in the file select menu and can have cache issues
        // if loaded before the randomizer finishes initializing
        const std::vector<std::string> criticalTextures = { "textures/parameter_static/*", "textures/title_static/*",
                                                            "textures/icon_item_static/*",
                                                            "textures/icon_item_24_static/*" };

        for (const auto& texturePath : criticalTextures) {
            // First unload to clear any stale cache entries
            resMgr->UnloadResources(texturePath);
            // Then immediately reload to ensure fresh, properly initialized textures
            resMgr->LoadResources(texturePath);
        }
    }
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
                Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
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
                    return INV_CONTENT(ITEM_BOMBCHU) == ITEM_BOMBCHU
                               ? (infiniteUpgrades != RO_INF_UPGRADES_OFF ? CAN_OBTAIN : CANT_OBTAIN_ALREADY_HAVE)
                               : CAN_OBTAIN;
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

// There has been some talk about potentially just using the RC identifier to store flags rather than randomizer inf, so
// for now we're not going to store randomzierInf in the randomizer check objects, we're just going to map them 1:1 here
std::map<RandomizerCheck, RandomizerInf> rcToRandomizerInf = {
    { RC_KF_LINKS_HOUSE_COW, RAND_INF_COWS_MILKED_KF_LINKS_HOUSE_COW },
    { RC_LW_DEKU_SCRUB_NEAR_DEKU_THEATER_RIGHT, RAND_INF_SCRUBS_PURCHASED_LW_DEKU_SCRUB_NEAR_DEKU_THEATER_RIGHT },
    { RC_LW_DEKU_SCRUB_NEAR_DEKU_THEATER_LEFT, RAND_INF_SCRUBS_PURCHASED_LW_DEKU_SCRUB_NEAR_DEKU_THEATER_LEFT },
    { RC_LW_DEKU_SCRUB_NEAR_BRIDGE, RAND_INF_SCRUBS_PURCHASED_LW_DEKU_SCRUB_NEAR_BRIDGE },
    { RC_LW_DEKU_SCRUB_GROTTO_REAR, RAND_INF_SCRUBS_PURCHASED_LW_DEKU_SCRUB_GROTTO_REAR },
    { RC_LW_DEKU_SCRUB_GROTTO_FRONT, RAND_INF_SCRUBS_PURCHASED_LW_DEKU_SCRUB_GROTTO_FRONT },
    { RC_SFM_DEKU_SCRUB_GROTTO_REAR, RAND_INF_SCRUBS_PURCHASED_SFM_DEKU_SCRUB_GROTTO_REAR },
    { RC_SFM_DEKU_SCRUB_GROTTO_FRONT, RAND_INF_SCRUBS_PURCHASED_SFM_DEKU_SCRUB_GROTTO_FRONT },
    { RC_HF_DEKU_SCRUB_GROTTO, RAND_INF_SCRUBS_PURCHASED_HF_DEKU_SCRUB_GROTTO },
    { RC_HF_COW_GROTTO_COW, RAND_INF_COWS_MILKED_HF_COW_GROTTO_COW },
    { RC_LH_DEKU_SCRUB_GROTTO_LEFT, RAND_INF_SCRUBS_PURCHASED_LH_DEKU_SCRUB_GROTTO_LEFT },
    { RC_LH_DEKU_SCRUB_GROTTO_RIGHT, RAND_INF_SCRUBS_PURCHASED_LH_DEKU_SCRUB_GROTTO_RIGHT },
    { RC_LH_DEKU_SCRUB_GROTTO_CENTER, RAND_INF_SCRUBS_PURCHASED_LH_DEKU_SCRUB_GROTTO_CENTER },
    { RC_GV_DEKU_SCRUB_GROTTO_REAR, RAND_INF_SCRUBS_PURCHASED_GV_DEKU_SCRUB_GROTTO_REAR },
    { RC_GV_DEKU_SCRUB_GROTTO_FRONT, RAND_INF_SCRUBS_PURCHASED_GV_DEKU_SCRUB_GROTTO_FRONT },
    { RC_GV_COW, RAND_INF_COWS_MILKED_GV_COW },
    { RC_COLOSSUS_DEKU_SCRUB_GROTTO_REAR, RAND_INF_SCRUBS_PURCHASED_COLOSSUS_DEKU_SCRUB_GROTTO_REAR },
    { RC_COLOSSUS_DEKU_SCRUB_GROTTO_FRONT, RAND_INF_SCRUBS_PURCHASED_COLOSSUS_DEKU_SCRUB_GROTTO_FRONT },
    { RC_KAK_IMPAS_HOUSE_COW, RAND_INF_COWS_MILKED_KAK_IMPAS_HOUSE_COW },
    { RC_DMT_COW_GROTTO_COW, RAND_INF_COWS_MILKED_DMT_COW_GROTTO_COW },
    { RC_GC_DEKU_SCRUB_GROTTO_LEFT, RAND_INF_SCRUBS_PURCHASED_GC_DEKU_SCRUB_GROTTO_LEFT },
    { RC_GC_DEKU_SCRUB_GROTTO_RIGHT, RAND_INF_SCRUBS_PURCHASED_GC_DEKU_SCRUB_GROTTO_RIGHT },
    { RC_GC_DEKU_SCRUB_GROTTO_CENTER, RAND_INF_SCRUBS_PURCHASED_GC_DEKU_SCRUB_GROTTO_CENTER },
    { RC_DMC_DEKU_SCRUB, RAND_INF_SCRUBS_PURCHASED_DMC_DEKU_SCRUB },
    { RC_DMC_DEKU_SCRUB_GROTTO_LEFT, RAND_INF_SCRUBS_PURCHASED_DMC_DEKU_SCRUB_GROTTO_LEFT },
    { RC_DMC_DEKU_SCRUB_GROTTO_RIGHT, RAND_INF_SCRUBS_PURCHASED_DMC_DEKU_SCRUB_GROTTO_RIGHT },
    { RC_DMC_DEKU_SCRUB_GROTTO_CENTER, RAND_INF_SCRUBS_PURCHASED_DMC_DEKU_SCRUB_GROTTO_CENTER },
    { RC_ZR_DEKU_SCRUB_GROTTO_REAR, RAND_INF_SCRUBS_PURCHASED_ZR_DEKU_SCRUB_GROTTO_REAR },
    { RC_ZR_DEKU_SCRUB_GROTTO_FRONT, RAND_INF_SCRUBS_PURCHASED_ZR_DEKU_SCRUB_GROTTO_FRONT },
    { RC_LLR_DEKU_SCRUB_GROTTO_LEFT, RAND_INF_SCRUBS_PURCHASED_LLR_DEKU_SCRUB_GROTTO_LEFT },
    { RC_LLR_DEKU_SCRUB_GROTTO_RIGHT, RAND_INF_SCRUBS_PURCHASED_LLR_DEKU_SCRUB_GROTTO_RIGHT },
    { RC_LLR_DEKU_SCRUB_GROTTO_CENTER, RAND_INF_SCRUBS_PURCHASED_LLR_DEKU_SCRUB_GROTTO_CENTER },
    { RC_LLR_STABLES_LEFT_COW, RAND_INF_COWS_MILKED_LLR_STABLES_LEFT_COW },
    { RC_LLR_STABLES_RIGHT_COW, RAND_INF_COWS_MILKED_LLR_STABLES_RIGHT_COW },
    { RC_LLR_TOWER_LEFT_COW, RAND_INF_COWS_MILKED_LLR_TOWER_LEFT_COW },
    { RC_LLR_TOWER_RIGHT_COW, RAND_INF_COWS_MILKED_LLR_TOWER_RIGHT_COW },
    { RC_DEKU_TREE_MQ_DEKU_SCRUB, RAND_INF_SCRUBS_PURCHASED_DEKU_TREE_MQ_DEKU_SCRUB },
    { RC_DODONGOS_CAVERN_DEKU_SCRUB_NEAR_BOMB_BAG_LEFT,
      RAND_INF_SCRUBS_PURCHASED_DODONGOS_CAVERN_DEKU_SCRUB_NEAR_BOMB_BAG_LEFT },
    { RC_DODONGOS_CAVERN_DEKU_SCRUB_SIDE_ROOM_NEAR_DODONGOS,
      RAND_INF_SCRUBS_PURCHASED_DODONGOS_CAVERN_DEKU_SCRUB_SIDE_ROOM_NEAR_DODONGOS },
    { RC_DODONGOS_CAVERN_DEKU_SCRUB_NEAR_BOMB_BAG_RIGHT,
      RAND_INF_SCRUBS_PURCHASED_DODONGOS_CAVERN_DEKU_SCRUB_NEAR_BOMB_BAG_RIGHT },
    { RC_DODONGOS_CAVERN_DEKU_SCRUB_LOBBY, RAND_INF_SCRUBS_PURCHASED_DODONGOS_CAVERN_DEKU_SCRUB_LOBBY },
    { RC_DODONGOS_CAVERN_MQ_DEKU_SCRUB_LOBBY_REAR, RAND_INF_SCRUBS_PURCHASED_DODONGOS_CAVERN_MQ_DEKU_SCRUB_LOBBY_REAR },
    { RC_DODONGOS_CAVERN_MQ_DEKU_SCRUB_LOBBY_FRONT,
      RAND_INF_SCRUBS_PURCHASED_DODONGOS_CAVERN_MQ_DEKU_SCRUB_LOBBY_FRONT },
    { RC_DODONGOS_CAVERN_MQ_DEKU_SCRUB_STAIRCASE, RAND_INF_SCRUBS_PURCHASED_DODONGOS_CAVERN_MQ_DEKU_SCRUB_STAIRCASE },
    { RC_DODONGOS_CAVERN_MQ_DEKU_SCRUB_SIDE_ROOM_NEAR_LOWER_LIZALFOS,
      RAND_INF_SCRUBS_PURCHASED_DODONGOS_CAVERN_MQ_DEKU_SCRUB_SIDE_ROOM_NEAR_LOWER_LIZALFOS },
    { RC_JABU_JABUS_BELLY_DEKU_SCRUB, RAND_INF_SCRUBS_PURCHASED_JABU_JABUS_BELLY_DEKU_SCRUB },
    { RC_JABU_JABUS_BELLY_MQ_COW, RAND_INF_COWS_MILKED_JABU_JABUS_BELLY_MQ_COW },
    { RC_GANONS_CASTLE_DEKU_SCRUB_CENTER_LEFT, RAND_INF_SCRUBS_PURCHASED_GANONS_CASTLE_DEKU_SCRUB_CENTER_LEFT },
    { RC_GANONS_CASTLE_DEKU_SCRUB_CENTER_RIGHT, RAND_INF_SCRUBS_PURCHASED_GANONS_CASTLE_DEKU_SCRUB_CENTER_RIGHT },
    { RC_GANONS_CASTLE_DEKU_SCRUB_RIGHT, RAND_INF_SCRUBS_PURCHASED_GANONS_CASTLE_DEKU_SCRUB_RIGHT },
    { RC_GANONS_CASTLE_DEKU_SCRUB_LEFT, RAND_INF_SCRUBS_PURCHASED_GANONS_CASTLE_DEKU_SCRUB_LEFT },
    { RC_GANONS_CASTLE_MQ_DEKU_SCRUB_RIGHT, RAND_INF_SCRUBS_PURCHASED_GANONS_CASTLE_MQ_DEKU_SCRUB_RIGHT },
    { RC_GANONS_CASTLE_MQ_DEKU_SCRUB_CENTER_LEFT, RAND_INF_SCRUBS_PURCHASED_GANONS_CASTLE_MQ_DEKU_SCRUB_CENTER_LEFT },
    { RC_GANONS_CASTLE_MQ_DEKU_SCRUB_CENTER, RAND_INF_SCRUBS_PURCHASED_GANONS_CASTLE_MQ_DEKU_SCRUB_CENTER },
    { RC_GANONS_CASTLE_MQ_DEKU_SCRUB_CENTER_RIGHT, RAND_INF_SCRUBS_PURCHASED_GANONS_CASTLE_MQ_DEKU_SCRUB_CENTER_RIGHT },
    { RC_GANONS_CASTLE_MQ_DEKU_SCRUB_LEFT, RAND_INF_SCRUBS_PURCHASED_GANONS_CASTLE_MQ_DEKU_SCRUB_LEFT },
    { RC_KF_SHOP_ITEM_1, RAND_INF_SHOP_ITEMS_KF_SHOP_ITEM_1 },
    { RC_KF_SHOP_ITEM_2, RAND_INF_SHOP_ITEMS_KF_SHOP_ITEM_2 },
    { RC_KF_SHOP_ITEM_3, RAND_INF_SHOP_ITEMS_KF_SHOP_ITEM_3 },
    { RC_KF_SHOP_ITEM_4, RAND_INF_SHOP_ITEMS_KF_SHOP_ITEM_4 },
    { RC_KF_SHOP_ITEM_5, RAND_INF_SHOP_ITEMS_KF_SHOP_ITEM_5 },
    { RC_KF_SHOP_ITEM_6, RAND_INF_SHOP_ITEMS_KF_SHOP_ITEM_6 },
    { RC_KF_SHOP_ITEM_7, RAND_INF_SHOP_ITEMS_KF_SHOP_ITEM_7 },
    { RC_KF_SHOP_ITEM_8, RAND_INF_SHOP_ITEMS_KF_SHOP_ITEM_8 },
    { RC_GC_SHOP_ITEM_1, RAND_INF_SHOP_ITEMS_GC_SHOP_ITEM_1 },
    { RC_GC_SHOP_ITEM_2, RAND_INF_SHOP_ITEMS_GC_SHOP_ITEM_2 },
    { RC_GC_SHOP_ITEM_3, RAND_INF_SHOP_ITEMS_GC_SHOP_ITEM_3 },
    { RC_GC_SHOP_ITEM_4, RAND_INF_SHOP_ITEMS_GC_SHOP_ITEM_4 },
    { RC_GC_SHOP_ITEM_5, RAND_INF_SHOP_ITEMS_GC_SHOP_ITEM_5 },
    { RC_GC_SHOP_ITEM_6, RAND_INF_SHOP_ITEMS_GC_SHOP_ITEM_6 },
    { RC_GC_SHOP_ITEM_7, RAND_INF_SHOP_ITEMS_GC_SHOP_ITEM_7 },
    { RC_GC_SHOP_ITEM_8, RAND_INF_SHOP_ITEMS_GC_SHOP_ITEM_8 },
    { RC_ZD_SHOP_ITEM_1, RAND_INF_SHOP_ITEMS_ZD_SHOP_ITEM_1 },
    { RC_ZD_SHOP_ITEM_2, RAND_INF_SHOP_ITEMS_ZD_SHOP_ITEM_2 },
    { RC_ZD_SHOP_ITEM_3, RAND_INF_SHOP_ITEMS_ZD_SHOP_ITEM_3 },
    { RC_ZD_SHOP_ITEM_4, RAND_INF_SHOP_ITEMS_ZD_SHOP_ITEM_4 },
    { RC_ZD_SHOP_ITEM_5, RAND_INF_SHOP_ITEMS_ZD_SHOP_ITEM_5 },
    { RC_ZD_SHOP_ITEM_6, RAND_INF_SHOP_ITEMS_ZD_SHOP_ITEM_6 },
    { RC_ZD_SHOP_ITEM_7, RAND_INF_SHOP_ITEMS_ZD_SHOP_ITEM_7 },
    { RC_ZD_SHOP_ITEM_8, RAND_INF_SHOP_ITEMS_ZD_SHOP_ITEM_8 },
    { RC_KAK_BAZAAR_ITEM_1, RAND_INF_SHOP_ITEMS_KAK_BAZAAR_ITEM_1 },
    { RC_KAK_BAZAAR_ITEM_2, RAND_INF_SHOP_ITEMS_KAK_BAZAAR_ITEM_2 },
    { RC_KAK_BAZAAR_ITEM_3, RAND_INF_SHOP_ITEMS_KAK_BAZAAR_ITEM_3 },
    { RC_KAK_BAZAAR_ITEM_4, RAND_INF_SHOP_ITEMS_KAK_BAZAAR_ITEM_4 },
    { RC_KAK_BAZAAR_ITEM_5, RAND_INF_SHOP_ITEMS_KAK_BAZAAR_ITEM_5 },
    { RC_KAK_BAZAAR_ITEM_6, RAND_INF_SHOP_ITEMS_KAK_BAZAAR_ITEM_6 },
    { RC_KAK_BAZAAR_ITEM_7, RAND_INF_SHOP_ITEMS_KAK_BAZAAR_ITEM_7 },
    { RC_KAK_BAZAAR_ITEM_8, RAND_INF_SHOP_ITEMS_KAK_BAZAAR_ITEM_8 },
    { RC_KAK_POTION_SHOP_ITEM_1, RAND_INF_SHOP_ITEMS_KAK_POTION_SHOP_ITEM_1 },
    { RC_KAK_POTION_SHOP_ITEM_2, RAND_INF_SHOP_ITEMS_KAK_POTION_SHOP_ITEM_2 },
    { RC_KAK_POTION_SHOP_ITEM_3, RAND_INF_SHOP_ITEMS_KAK_POTION_SHOP_ITEM_3 },
    { RC_KAK_POTION_SHOP_ITEM_4, RAND_INF_SHOP_ITEMS_KAK_POTION_SHOP_ITEM_4 },
    { RC_KAK_POTION_SHOP_ITEM_5, RAND_INF_SHOP_ITEMS_KAK_POTION_SHOP_ITEM_5 },
    { RC_KAK_POTION_SHOP_ITEM_6, RAND_INF_SHOP_ITEMS_KAK_POTION_SHOP_ITEM_6 },
    { RC_KAK_POTION_SHOP_ITEM_7, RAND_INF_SHOP_ITEMS_KAK_POTION_SHOP_ITEM_7 },
    { RC_KAK_POTION_SHOP_ITEM_8, RAND_INF_SHOP_ITEMS_KAK_POTION_SHOP_ITEM_8 },
    { RC_MARKET_BAZAAR_ITEM_1, RAND_INF_SHOP_ITEMS_MARKET_BAZAAR_ITEM_1 },
    { RC_MARKET_BAZAAR_ITEM_2, RAND_INF_SHOP_ITEMS_MARKET_BAZAAR_ITEM_2 },
    { RC_MARKET_BAZAAR_ITEM_3, RAND_INF_SHOP_ITEMS_MARKET_BAZAAR_ITEM_3 },
    { RC_MARKET_BAZAAR_ITEM_4, RAND_INF_SHOP_ITEMS_MARKET_BAZAAR_ITEM_4 },
    { RC_MARKET_BAZAAR_ITEM_5, RAND_INF_SHOP_ITEMS_MARKET_BAZAAR_ITEM_5 },
    { RC_MARKET_BAZAAR_ITEM_6, RAND_INF_SHOP_ITEMS_MARKET_BAZAAR_ITEM_6 },
    { RC_MARKET_BAZAAR_ITEM_7, RAND_INF_SHOP_ITEMS_MARKET_BAZAAR_ITEM_7 },
    { RC_MARKET_BAZAAR_ITEM_8, RAND_INF_SHOP_ITEMS_MARKET_BAZAAR_ITEM_8 },
    { RC_MARKET_POTION_SHOP_ITEM_1, RAND_INF_SHOP_ITEMS_MARKET_POTION_SHOP_ITEM_1 },
    { RC_MARKET_POTION_SHOP_ITEM_2, RAND_INF_SHOP_ITEMS_MARKET_POTION_SHOP_ITEM_2 },
    { RC_MARKET_POTION_SHOP_ITEM_3, RAND_INF_SHOP_ITEMS_MARKET_POTION_SHOP_ITEM_3 },
    { RC_MARKET_POTION_SHOP_ITEM_4, RAND_INF_SHOP_ITEMS_MARKET_POTION_SHOP_ITEM_4 },
    { RC_MARKET_POTION_SHOP_ITEM_5, RAND_INF_SHOP_ITEMS_MARKET_POTION_SHOP_ITEM_5 },
    { RC_MARKET_POTION_SHOP_ITEM_6, RAND_INF_SHOP_ITEMS_MARKET_POTION_SHOP_ITEM_6 },
    { RC_MARKET_POTION_SHOP_ITEM_7, RAND_INF_SHOP_ITEMS_MARKET_POTION_SHOP_ITEM_7 },
    { RC_MARKET_POTION_SHOP_ITEM_8, RAND_INF_SHOP_ITEMS_MARKET_POTION_SHOP_ITEM_8 },
    { RC_MARKET_BOMBCHU_SHOP_ITEM_1, RAND_INF_SHOP_ITEMS_MARKET_BOMBCHU_SHOP_ITEM_1 },
    { RC_MARKET_BOMBCHU_SHOP_ITEM_2, RAND_INF_SHOP_ITEMS_MARKET_BOMBCHU_SHOP_ITEM_2 },
    { RC_MARKET_BOMBCHU_SHOP_ITEM_3, RAND_INF_SHOP_ITEMS_MARKET_BOMBCHU_SHOP_ITEM_3 },
    { RC_MARKET_BOMBCHU_SHOP_ITEM_4, RAND_INF_SHOP_ITEMS_MARKET_BOMBCHU_SHOP_ITEM_4 },
    { RC_MARKET_BOMBCHU_SHOP_ITEM_5, RAND_INF_SHOP_ITEMS_MARKET_BOMBCHU_SHOP_ITEM_5 },
    { RC_MARKET_BOMBCHU_SHOP_ITEM_6, RAND_INF_SHOP_ITEMS_MARKET_BOMBCHU_SHOP_ITEM_6 },
    { RC_MARKET_BOMBCHU_SHOP_ITEM_7, RAND_INF_SHOP_ITEMS_MARKET_BOMBCHU_SHOP_ITEM_7 },
    { RC_MARKET_BOMBCHU_SHOP_ITEM_8, RAND_INF_SHOP_ITEMS_MARKET_BOMBCHU_SHOP_ITEM_8 },
    { RC_TOT_MASTER_SWORD, RAND_INF_TOT_MASTER_SWORD },
    { RC_GC_MEDIGORON, RAND_INF_MERCHANTS_MEDIGORON },
    { RC_KAK_GRANNYS_SHOP, RAND_INF_MERCHANTS_GRANNYS_SHOP },
    { RC_WASTELAND_BOMBCHU_SALESMAN, RAND_INF_MERCHANTS_CARPET_SALESMAN },
    { RC_ZR_MAGIC_BEAN_SALESMAN, RAND_INF_MERCHANTS_MAGIC_BEAN_SALESMAN },
    { RC_LW_TRADE_COJIRO, RAND_INF_ADULT_TRADES_LW_TRADE_COJIRO },
    { RC_GV_TRADE_SAW, RAND_INF_ADULT_TRADES_GV_TRADE_SAW },
    { RC_DMT_TRADE_BROKEN_SWORD, RAND_INF_ADULT_TRADES_DMT_TRADE_BROKEN_SWORD },
    { RC_LH_TRADE_FROG, RAND_INF_ADULT_TRADES_LH_TRADE_FROG },
    { RC_DMT_TRADE_EYEDROPS, RAND_INF_ADULT_TRADES_DMT_TRADE_EYEDROPS },
    { RC_LH_CHILD_FISHING, RAND_INF_CHILD_FISHING },
    { RC_LH_ADULT_FISHING, RAND_INF_ADULT_FISHING },
    { RC_MARKET_10_BIG_POES, RAND_INF_10_BIG_POES },
    { RC_KAK_100_GOLD_SKULLTULA_REWARD, RAND_INF_KAK_100_GOLD_SKULLTULA_REWARD },
    { RC_KF_STORMS_GROTTO_BEEHIVE_LEFT, RAND_INF_BEEHIVE_KF_STORMS_GROTTO_LEFT },
    { RC_KF_STORMS_GROTTO_BEEHIVE_RIGHT, RAND_INF_BEEHIVE_KF_STORMS_GROTTO_RIGHT },
    { RC_LW_NEAR_SHORTCUTS_GROTTO_BEEHIVE_LEFT, RAND_INF_BEEHIVE_LW_NEAR_SHORTCUTS_GROTTO_LEFT },
    { RC_LW_NEAR_SHORTCUTS_GROTTO_BEEHIVE_RIGHT, RAND_INF_BEEHIVE_LW_NEAR_SHORTCUTS_GROTTO_RIGHT },
    { RC_LW_DEKU_SCRUB_GROTTO_BEEHIVE, RAND_INF_BEEHIVE_LW_DEKU_SCRUB_GROTTO },
    { RC_SFM_STORMS_GROTTO_BEEHIVE, RAND_INF_BEEHIVE_SFM_STORMS_GROTTO },
    { RC_HF_NEAR_MARKET_GROTTO_BEEHIVE_LEFT, RAND_INF_BEEHIVE_HF_NEAR_MARKET_GROTTO_LEFT },
    { RC_HF_NEAR_MARKET_GROTTO_BEEHIVE_RIGHT, RAND_INF_BEEHIVE_HF_NEAR_MARKET_GROTTO_RIGHT },
    { RC_HF_OPEN_GROTTO_BEEHIVE_LEFT, RAND_INF_BEEHIVE_HF_OPEN_GROTTO_LEFT },
    { RC_HF_OPEN_GROTTO_BEEHIVE_RIGHT, RAND_INF_BEEHIVE_HF_OPEN_GROTTO_RIGHT },
    { RC_HF_SOUTHEAST_GROTTO_BEEHIVE_LEFT, RAND_INF_BEEHIVE_HF_SOUTHEAST_GROTTO_LEFT },
    { RC_HF_SOUTHEAST_GROTTO_BEEHIVE_RIGHT, RAND_INF_BEEHIVE_HF_SOUTHEAST_GROTTO_RIGHT },
    { RC_HF_INSIDE_FENCE_GROTTO_BEEHIVE, RAND_INF_BEEHIVE_HF_INSIDE_FENCE_GROTTO },
    { RC_LLR_GROTTO_BEEHIVE, RAND_INF_BEEHIVE_LLR_GROTTO },
    { RC_KAK_OPEN_GROTTO_BEEHIVE_LEFT, RAND_INF_BEEHIVE_KAK_OPEN_GROTTO_LEFT },
    { RC_KAK_OPEN_GROTTO_BEEHIVE_RIGHT, RAND_INF_BEEHIVE_KAK_OPEN_GROTTO_RIGHT },
    { RC_DMT_COW_GROTTO_BEEHIVE, RAND_INF_BEEHIVE_DMT_COW_GROTTO },
    { RC_DMT_STORMS_GROTTO_BEEHIVE_LEFT, RAND_INF_BEEHIVE_DMT_STORMS_GROTTO_LEFT },
    { RC_DMT_STORMS_GROTTO_BEEHIVE_RIGHT, RAND_INF_BEEHIVE_DMT_STORMS_GROTTO_RIGHT },
    { RC_GC_GROTTO_BEEHIVE, RAND_INF_BEEHIVE_GC_GROTTO },
    { RC_DMC_UPPER_GROTTO_BEEHIVE_LEFT, RAND_INF_BEEHIVE_DMC_UPPER_GROTTO_LEFT },
    { RC_DMC_UPPER_GROTTO_BEEHIVE_RIGHT, RAND_INF_BEEHIVE_DMC_UPPER_GROTTO_RIGHT },
    { RC_DMC_HAMMER_GROTTO_BEEHIVE, RAND_INF_BEEHIVE_DMC_HAMMER_GROTTO },
    { RC_ZR_OPEN_GROTTO_BEEHIVE_LEFT, RAND_INF_BEEHIVE_ZR_OPEN_GROTTO_LEFT },
    { RC_ZR_OPEN_GROTTO_BEEHIVE_RIGHT, RAND_INF_BEEHIVE_ZR_OPEN_GROTTO_RIGHT },
    { RC_ZR_STORMS_GROTTO_BEEHIVE, RAND_INF_BEEHIVE_ZR_STORMS_GROTTO },
    { RC_ZD_IN_FRONT_OF_KING_ZORA_BEEHIVE_LEFT, RAND_INF_BEEHIVE_ZD_IN_FRONT_OF_KING_ZORA_LEFT },
    { RC_ZD_IN_FRONT_OF_KING_ZORA_BEEHIVE_RIGHT, RAND_INF_BEEHIVE_ZD_IN_FRONT_OF_KING_ZORA_RIGHT },
    { RC_ZD_BEHIND_KING_ZORA_BEEHIVE, RAND_INF_BEEHIVE_ZD_BEHIND_KING_ZORA },
    { RC_LH_GROTTO_BEEHIVE, RAND_INF_BEEHIVE_LH_GROTTO },
    { RC_GV_DEKU_SCRUB_GROTTO_BEEHIVE, RAND_INF_BEEHIVE_GV_DEKU_SCRUB_GROTTO },
    { RC_COLOSSUS_GROTTO_BEEHIVE, RAND_INF_BEEHIVE_COLOSSUS_GROTTO },
    { RC_LH_CHILD_FISH_1, RAND_INF_CHILD_FISH_1 },
    { RC_LH_CHILD_FISH_2, RAND_INF_CHILD_FISH_2 },
    { RC_LH_CHILD_FISH_3, RAND_INF_CHILD_FISH_3 },
    { RC_LH_CHILD_FISH_4, RAND_INF_CHILD_FISH_4 },
    { RC_LH_CHILD_FISH_5, RAND_INF_CHILD_FISH_5 },
    { RC_LH_CHILD_FISH_6, RAND_INF_CHILD_FISH_6 },
    { RC_LH_CHILD_FISH_7, RAND_INF_CHILD_FISH_7 },
    { RC_LH_CHILD_FISH_8, RAND_INF_CHILD_FISH_8 },
    { RC_LH_CHILD_FISH_9, RAND_INF_CHILD_FISH_9 },
    { RC_LH_CHILD_FISH_10, RAND_INF_CHILD_FISH_10 },
    { RC_LH_CHILD_FISH_11, RAND_INF_CHILD_FISH_11 },
    { RC_LH_CHILD_FISH_12, RAND_INF_CHILD_FISH_12 },
    { RC_LH_CHILD_FISH_13, RAND_INF_CHILD_FISH_13 },
    { RC_LH_CHILD_FISH_14, RAND_INF_CHILD_FISH_14 },
    { RC_LH_CHILD_FISH_15, RAND_INF_CHILD_FISH_15 },
    { RC_LH_CHILD_LOACH_1, RAND_INF_CHILD_LOACH_1 },
    { RC_LH_CHILD_LOACH_2, RAND_INF_CHILD_LOACH_2 },
    { RC_LH_ADULT_FISH_1, RAND_INF_ADULT_FISH_1 },
    { RC_LH_ADULT_FISH_2, RAND_INF_ADULT_FISH_2 },
    { RC_LH_ADULT_FISH_3, RAND_INF_ADULT_FISH_3 },
    { RC_LH_ADULT_FISH_4, RAND_INF_ADULT_FISH_4 },
    { RC_LH_ADULT_FISH_5, RAND_INF_ADULT_FISH_5 },
    { RC_LH_ADULT_FISH_6, RAND_INF_ADULT_FISH_6 },
    { RC_LH_ADULT_FISH_7, RAND_INF_ADULT_FISH_7 },
    { RC_LH_ADULT_FISH_8, RAND_INF_ADULT_FISH_8 },
    { RC_LH_ADULT_FISH_9, RAND_INF_ADULT_FISH_9 },
    { RC_LH_ADULT_FISH_10, RAND_INF_ADULT_FISH_10 },
    { RC_LH_ADULT_FISH_11, RAND_INF_ADULT_FISH_11 },
    { RC_LH_ADULT_FISH_12, RAND_INF_ADULT_FISH_12 },
    { RC_LH_ADULT_FISH_13, RAND_INF_ADULT_FISH_13 },
    { RC_LH_ADULT_FISH_14, RAND_INF_ADULT_FISH_14 },
    { RC_LH_ADULT_FISH_15, RAND_INF_ADULT_FISH_15 },
    { RC_LH_ADULT_LOACH, RAND_INF_ADULT_LOACH },
    { RC_ZR_OPEN_GROTTO_FISH, RAND_INF_GROTTO_FISH_ZR_OPEN_GROTTO },
    { RC_DMC_UPPER_GROTTO_FISH, RAND_INF_GROTTO_FISH_DMC_UPPER_GROTTO },
    { RC_DMT_STORMS_GROTTO_FISH, RAND_INF_GROTTO_FISH_DMT_STORMS_GROTTO },
    { RC_KAK_OPEN_GROTTO_FISH, RAND_INF_GROTTO_FISH_KAK_OPEN_GROTTO },
    { RC_HF_NEAR_MARKET_GROTTO_FISH, RAND_INF_GROTTO_FISH_HF_NEAR_MARKET_GROTTO },
    { RC_HF_OPEN_GROTTO_FISH, RAND_INF_GROTTO_FISH_HF_OPEN_GROTTO },
    { RC_HF_SOUTHEAST_GROTTO_FISH, RAND_INF_GROTTO_FISH_HF_SOUTHEAST_GROTTO },
    { RC_LW_NEAR_SHORTCUTS_GROTTO_FISH, RAND_INF_GROTTO_FISH_LW_NEAR_SHORTCUTS_GROTTO },
    { RC_KF_STORMS_GROTTO_FISH, RAND_INF_GROTTO_FISH_KF_STORMS_GROTTO },
    { RC_ZD_FISH_1, RAND_INF_ZD_FISH_1 },
    { RC_ZD_FISH_2, RAND_INF_ZD_FISH_2 },
    { RC_ZD_FISH_3, RAND_INF_ZD_FISH_3 },
    { RC_ZD_FISH_4, RAND_INF_ZD_FISH_4 },
    { RC_ZD_FISH_5, RAND_INF_ZD_FISH_5 },
    // Grass
    { RC_KF_CHILD_GRASS_1, RAND_INF_KF_CHILD_GRASS_1 },
    { RC_KF_CHILD_GRASS_2, RAND_INF_KF_CHILD_GRASS_2 },
    { RC_KF_CHILD_GRASS_3, RAND_INF_KF_CHILD_GRASS_3 },
    { RC_KF_CHILD_GRASS_4, RAND_INF_KF_CHILD_GRASS_4 },
    { RC_KF_CHILD_GRASS_5, RAND_INF_KF_CHILD_GRASS_5 },
    { RC_KF_CHILD_GRASS_6, RAND_INF_KF_CHILD_GRASS_6 },
    { RC_KF_CHILD_GRASS_7, RAND_INF_KF_CHILD_GRASS_7 },
    { RC_KF_CHILD_GRASS_8, RAND_INF_KF_CHILD_GRASS_8 },
    { RC_KF_CHILD_GRASS_9, RAND_INF_KF_CHILD_GRASS_9 },
    { RC_KF_CHILD_GRASS_10, RAND_INF_KF_CHILD_GRASS_10 },
    { RC_KF_CHILD_GRASS_11, RAND_INF_KF_CHILD_GRASS_11 },
    { RC_KF_CHILD_GRASS_12, RAND_INF_KF_CHILD_GRASS_12 },
    { RC_KF_CHILD_GRASS_MAZE_1, RAND_INF_KF_CHILD_GRASS_MAZE_1 },
    { RC_KF_CHILD_GRASS_MAZE_2, RAND_INF_KF_CHILD_GRASS_MAZE_2 },
    { RC_KF_CHILD_GRASS_MAZE_3, RAND_INF_KF_CHILD_GRASS_MAZE_3 },
    { RC_KF_ADULT_GRASS_1, RAND_INF_KF_ADULT_GRASS_1 },
    { RC_KF_ADULT_GRASS_2, RAND_INF_KF_ADULT_GRASS_2 },
    { RC_KF_ADULT_GRASS_3, RAND_INF_KF_ADULT_GRASS_3 },
    { RC_KF_ADULT_GRASS_4, RAND_INF_KF_ADULT_GRASS_4 },
    { RC_KF_ADULT_GRASS_5, RAND_INF_KF_ADULT_GRASS_5 },
    { RC_KF_ADULT_GRASS_6, RAND_INF_KF_ADULT_GRASS_6 },
    { RC_KF_ADULT_GRASS_7, RAND_INF_KF_ADULT_GRASS_7 },
    { RC_KF_ADULT_GRASS_8, RAND_INF_KF_ADULT_GRASS_8 },
    { RC_KF_ADULT_GRASS_9, RAND_INF_KF_ADULT_GRASS_9 },
    { RC_KF_ADULT_GRASS_10, RAND_INF_KF_ADULT_GRASS_10 },
    { RC_KF_ADULT_GRASS_11, RAND_INF_KF_ADULT_GRASS_11 },
    { RC_KF_ADULT_GRASS_12, RAND_INF_KF_ADULT_GRASS_12 },
    { RC_KF_ADULT_GRASS_13, RAND_INF_KF_ADULT_GRASS_13 },
    { RC_KF_ADULT_GRASS_14, RAND_INF_KF_ADULT_GRASS_14 },
    { RC_KF_ADULT_GRASS_15, RAND_INF_KF_ADULT_GRASS_15 },
    { RC_KF_ADULT_GRASS_16, RAND_INF_KF_ADULT_GRASS_16 },
    { RC_KF_ADULT_GRASS_17, RAND_INF_KF_ADULT_GRASS_17 },
    { RC_KF_ADULT_GRASS_18, RAND_INF_KF_ADULT_GRASS_18 },
    { RC_KF_ADULT_GRASS_19, RAND_INF_KF_ADULT_GRASS_19 },
    { RC_KF_ADULT_GRASS_20, RAND_INF_KF_ADULT_GRASS_20 },
    { RC_LW_GRASS_1, RAND_INF_LW_GRASS_1 },
    { RC_LW_GRASS_2, RAND_INF_LW_GRASS_2 },
    { RC_LW_GRASS_3, RAND_INF_LW_GRASS_3 },
    { RC_LW_GRASS_4, RAND_INF_LW_GRASS_4 },
    { RC_LW_GRASS_5, RAND_INF_LW_GRASS_5 },
    { RC_LW_GRASS_6, RAND_INF_LW_GRASS_6 },
    { RC_LW_GRASS_7, RAND_INF_LW_GRASS_7 },
    { RC_LW_GRASS_8, RAND_INF_LW_GRASS_8 },
    { RC_LW_GRASS_9, RAND_INF_LW_GRASS_9 },
    { RC_MARKET_GRASS_1, RAND_INF_MARKET_GRASS_1 },
    { RC_MARKET_GRASS_2, RAND_INF_MARKET_GRASS_2 },
    { RC_MARKET_GRASS_3, RAND_INF_MARKET_GRASS_3 },
    { RC_MARKET_GRASS_4, RAND_INF_MARKET_GRASS_4 },
    { RC_MARKET_GRASS_5, RAND_INF_MARKET_GRASS_5 },
    { RC_MARKET_GRASS_6, RAND_INF_MARKET_GRASS_6 },
    { RC_MARKET_GRASS_7, RAND_INF_MARKET_GRASS_7 },
    { RC_MARKET_GRASS_8, RAND_INF_MARKET_GRASS_8 },
    { RC_HC_GRASS_1, RAND_INF_HC_GRASS_1 },
    { RC_HC_GRASS_2, RAND_INF_HC_GRASS_2 },
    { RC_KAK_GRASS_1, RAND_INF_KAK_GRASS_1 },
    { RC_KAK_GRASS_2, RAND_INF_KAK_GRASS_2 },
    { RC_KAK_GRASS_3, RAND_INF_KAK_GRASS_3 },
    { RC_KAK_GRASS_4, RAND_INF_KAK_GRASS_4 },
    { RC_KAK_GRASS_5, RAND_INF_KAK_GRASS_5 },
    { RC_KAK_GRASS_6, RAND_INF_KAK_GRASS_6 },
    { RC_KAK_GRASS_7, RAND_INF_KAK_GRASS_7 },
    { RC_KAK_GRASS_8, RAND_INF_KAK_GRASS_8 },
    { RC_GY_GRASS_1, RAND_INF_GY_GRASS_1 },
    { RC_GY_GRASS_2, RAND_INF_GY_GRASS_2 },
    { RC_GY_GRASS_3, RAND_INF_GY_GRASS_3 },
    { RC_GY_GRASS_4, RAND_INF_GY_GRASS_4 },
    { RC_GY_GRASS_5, RAND_INF_GY_GRASS_5 },
    { RC_GY_GRASS_6, RAND_INF_GY_GRASS_6 },
    { RC_GY_GRASS_7, RAND_INF_GY_GRASS_7 },
    { RC_GY_GRASS_8, RAND_INF_GY_GRASS_8 },
    { RC_GY_GRASS_9, RAND_INF_GY_GRASS_9 },
    { RC_GY_GRASS_10, RAND_INF_GY_GRASS_10 },
    { RC_GY_GRASS_11, RAND_INF_GY_GRASS_11 },
    { RC_GY_GRASS_12, RAND_INF_GY_GRASS_12 },
    { RC_LH_GRASS_1, RAND_INF_LH_GRASS_1 },
    { RC_LH_GRASS_2, RAND_INF_LH_GRASS_2 },
    { RC_LH_GRASS_3, RAND_INF_LH_GRASS_3 },
    { RC_LH_GRASS_4, RAND_INF_LH_GRASS_4 },
    { RC_LH_GRASS_5, RAND_INF_LH_GRASS_5 },
    { RC_LH_GRASS_6, RAND_INF_LH_GRASS_6 },
    { RC_LH_GRASS_7, RAND_INF_LH_GRASS_7 },
    { RC_LH_GRASS_8, RAND_INF_LH_GRASS_8 },
    { RC_LH_GRASS_9, RAND_INF_LH_GRASS_9 },
    { RC_LH_GRASS_10, RAND_INF_LH_GRASS_10 },
    { RC_LH_GRASS_11, RAND_INF_LH_GRASS_11 },
    { RC_LH_GRASS_12, RAND_INF_LH_GRASS_12 },
    { RC_LH_GRASS_13, RAND_INF_LH_GRASS_13 },
    { RC_LH_GRASS_14, RAND_INF_LH_GRASS_14 },
    { RC_LH_GRASS_15, RAND_INF_LH_GRASS_15 },
    { RC_LH_GRASS_16, RAND_INF_LH_GRASS_16 },
    { RC_LH_GRASS_17, RAND_INF_LH_GRASS_17 },
    { RC_LH_GRASS_18, RAND_INF_LH_GRASS_18 },
    { RC_LH_GRASS_19, RAND_INF_LH_GRASS_19 },
    { RC_LH_GRASS_20, RAND_INF_LH_GRASS_20 },
    { RC_LH_GRASS_21, RAND_INF_LH_GRASS_21 },
    { RC_LH_GRASS_22, RAND_INF_LH_GRASS_22 },
    { RC_LH_GRASS_23, RAND_INF_LH_GRASS_23 },
    { RC_LH_GRASS_24, RAND_INF_LH_GRASS_24 },
    { RC_LH_GRASS_25, RAND_INF_LH_GRASS_25 },
    { RC_LH_GRASS_26, RAND_INF_LH_GRASS_26 },
    { RC_LH_GRASS_27, RAND_INF_LH_GRASS_27 },
    { RC_LH_GRASS_28, RAND_INF_LH_GRASS_28 },
    { RC_LH_GRASS_29, RAND_INF_LH_GRASS_29 },
    { RC_LH_GRASS_30, RAND_INF_LH_GRASS_30 },
    { RC_LH_GRASS_31, RAND_INF_LH_GRASS_31 },
    { RC_LH_GRASS_32, RAND_INF_LH_GRASS_32 },
    { RC_LH_GRASS_33, RAND_INF_LH_GRASS_33 },
    { RC_LH_GRASS_34, RAND_INF_LH_GRASS_34 },
    { RC_LH_GRASS_35, RAND_INF_LH_GRASS_35 },
    { RC_LH_GRASS_36, RAND_INF_LH_GRASS_36 },
    { RC_LH_CHILD_GRASS_1, RAND_INF_LH_CHILD_GRASS_1 },
    { RC_LH_CHILD_GRASS_2, RAND_INF_LH_CHILD_GRASS_2 },
    { RC_LH_CHILD_GRASS_3, RAND_INF_LH_CHILD_GRASS_3 },
    { RC_LH_CHILD_GRASS_4, RAND_INF_LH_CHILD_GRASS_4 },
    { RC_LH_WARP_PAD_GRASS_1, RAND_INF_LH_WARP_PAD_GRASS_1 },
    { RC_LH_WARP_PAD_GRASS_2, RAND_INF_LH_WARP_PAD_GRASS_2 },
    { RC_HF_NEAR_KF_GRASS_1, RAND_INF_HF_NEAR_KF_GRASS_1 },
    { RC_HF_NEAR_KF_GRASS_2, RAND_INF_HF_NEAR_KF_GRASS_2 },
    { RC_HF_NEAR_KF_GRASS_3, RAND_INF_HF_NEAR_KF_GRASS_3 },
    { RC_HF_NEAR_KF_GRASS_4, RAND_INF_HF_NEAR_KF_GRASS_4 },
    { RC_HF_NEAR_KF_GRASS_5, RAND_INF_HF_NEAR_KF_GRASS_5 },
    { RC_HF_NEAR_KF_GRASS_6, RAND_INF_HF_NEAR_KF_GRASS_6 },
    { RC_HF_NEAR_KF_GRASS_7, RAND_INF_HF_NEAR_KF_GRASS_7 },
    { RC_HF_NEAR_KF_GRASS_8, RAND_INF_HF_NEAR_KF_GRASS_8 },
    { RC_HF_NEAR_KF_GRASS_9, RAND_INF_HF_NEAR_KF_GRASS_9 },
    { RC_HF_NEAR_KF_GRASS_10, RAND_INF_HF_NEAR_KF_GRASS_10 },
    { RC_HF_NEAR_KF_GRASS_11, RAND_INF_HF_NEAR_KF_GRASS_11 },
    { RC_HF_NEAR_KF_GRASS_12, RAND_INF_HF_NEAR_KF_GRASS_12 },
    { RC_HF_NEAR_MARKET_GRASS_1, RAND_INF_HF_NEAR_MARKET_GRASS_1 },
    { RC_HF_NEAR_MARKET_GRASS_2, RAND_INF_HF_NEAR_MARKET_GRASS_2 },
    { RC_HF_NEAR_MARKET_GRASS_3, RAND_INF_HF_NEAR_MARKET_GRASS_3 },
    { RC_HF_NEAR_MARKET_GRASS_4, RAND_INF_HF_NEAR_MARKET_GRASS_4 },
    { RC_HF_NEAR_MARKET_GRASS_5, RAND_INF_HF_NEAR_MARKET_GRASS_5 },
    { RC_HF_NEAR_MARKET_GRASS_6, RAND_INF_HF_NEAR_MARKET_GRASS_6 },
    { RC_HF_NEAR_MARKET_GRASS_7, RAND_INF_HF_NEAR_MARKET_GRASS_7 },
    { RC_HF_NEAR_MARKET_GRASS_8, RAND_INF_HF_NEAR_MARKET_GRASS_8 },
    { RC_HF_NEAR_MARKET_GRASS_9, RAND_INF_HF_NEAR_MARKET_GRASS_9 },
    { RC_HF_NEAR_MARKET_GRASS_10, RAND_INF_HF_NEAR_MARKET_GRASS_10 },
    { RC_HF_NEAR_MARKET_GRASS_11, RAND_INF_HF_NEAR_MARKET_GRASS_11 },
    { RC_HF_NEAR_MARKET_GRASS_12, RAND_INF_HF_NEAR_MARKET_GRASS_12 },
    { RC_HF_SOUTH_GRASS_1, RAND_INF_HF_SOUTH_GRASS_1 },
    { RC_HF_SOUTH_GRASS_2, RAND_INF_HF_SOUTH_GRASS_2 },
    { RC_HF_SOUTH_GRASS_3, RAND_INF_HF_SOUTH_GRASS_3 },
    { RC_HF_SOUTH_GRASS_4, RAND_INF_HF_SOUTH_GRASS_4 },
    { RC_HF_SOUTH_GRASS_5, RAND_INF_HF_SOUTH_GRASS_5 },
    { RC_HF_SOUTH_GRASS_6, RAND_INF_HF_SOUTH_GRASS_6 },
    { RC_HF_SOUTH_GRASS_7, RAND_INF_HF_SOUTH_GRASS_7 },
    { RC_HF_SOUTH_GRASS_8, RAND_INF_HF_SOUTH_GRASS_8 },
    { RC_HF_SOUTH_GRASS_9, RAND_INF_HF_SOUTH_GRASS_9 },
    { RC_HF_SOUTH_GRASS_10, RAND_INF_HF_SOUTH_GRASS_10 },
    { RC_HF_SOUTH_GRASS_11, RAND_INF_HF_SOUTH_GRASS_11 },
    { RC_HF_SOUTH_GRASS_12, RAND_INF_HF_SOUTH_GRASS_12 },
    { RC_HF_CENTRAL_GRASS_1, RAND_INF_HF_CENTRAL_GRASS_1 },
    { RC_HF_CENTRAL_GRASS_2, RAND_INF_HF_CENTRAL_GRASS_2 },
    { RC_HF_CENTRAL_GRASS_3, RAND_INF_HF_CENTRAL_GRASS_3 },
    { RC_HF_CENTRAL_GRASS_4, RAND_INF_HF_CENTRAL_GRASS_4 },
    { RC_HF_CENTRAL_GRASS_5, RAND_INF_HF_CENTRAL_GRASS_5 },
    { RC_HF_CENTRAL_GRASS_6, RAND_INF_HF_CENTRAL_GRASS_6 },
    { RC_HF_CENTRAL_GRASS_7, RAND_INF_HF_CENTRAL_GRASS_7 },
    { RC_HF_CENTRAL_GRASS_8, RAND_INF_HF_CENTRAL_GRASS_8 },
    { RC_HF_CENTRAL_GRASS_9, RAND_INF_HF_CENTRAL_GRASS_9 },
    { RC_HF_CENTRAL_GRASS_10, RAND_INF_HF_CENTRAL_GRASS_10 },
    { RC_HF_CENTRAL_GRASS_11, RAND_INF_HF_CENTRAL_GRASS_11 },
    { RC_HF_CENTRAL_GRASS_12, RAND_INF_HF_CENTRAL_GRASS_12 },
    { RC_ZR_GRASS_1, RAND_INF_ZR_GRASS_1 },
    { RC_ZR_GRASS_2, RAND_INF_ZR_GRASS_2 },
    { RC_ZR_GRASS_3, RAND_INF_ZR_GRASS_3 },
    { RC_ZR_GRASS_4, RAND_INF_ZR_GRASS_4 },
    { RC_ZR_GRASS_5, RAND_INF_ZR_GRASS_5 },
    { RC_ZR_GRASS_6, RAND_INF_ZR_GRASS_6 },
    { RC_ZR_GRASS_7, RAND_INF_ZR_GRASS_7 },
    { RC_ZR_GRASS_8, RAND_INF_ZR_GRASS_8 },
    { RC_ZR_GRASS_9, RAND_INF_ZR_GRASS_9 },
    { RC_ZR_GRASS_10, RAND_INF_ZR_GRASS_10 },
    { RC_ZR_GRASS_11, RAND_INF_ZR_GRASS_11 },
    { RC_ZR_GRASS_12, RAND_INF_ZR_GRASS_12 },
    { RC_ZR_NEAR_FREESTANDING_POH_GRASS, RAND_INF_ZR_NEAR_FREESTANDING_POH_GRASS },
    // Grotto Grass
    { RC_KF_STORMS_GROTTO_GRASS_1, RAND_INF_KF_STORMS_GROTTO_GRASS_1 },
    { RC_KF_STORMS_GROTTO_GRASS_2, RAND_INF_KF_STORMS_GROTTO_GRASS_2 },
    { RC_KF_STORMS_GROTTO_GRASS_3, RAND_INF_KF_STORMS_GROTTO_GRASS_3 },
    { RC_KF_STORMS_GROTTO_GRASS_4, RAND_INF_KF_STORMS_GROTTO_GRASS_4 },
    { RC_LW_NEAR_SHORTCUTS_GROTTO_GRASS_1, RAND_INF_LW_NEAR_SHORTCUTS_GROTTO_GRASS_1 },
    { RC_LW_NEAR_SHORTCUTS_GROTTO_GRASS_2, RAND_INF_LW_NEAR_SHORTCUTS_GROTTO_GRASS_2 },
    { RC_LW_NEAR_SHORTCUTS_GROTTO_GRASS_3, RAND_INF_LW_NEAR_SHORTCUTS_GROTTO_GRASS_3 },
    { RC_LW_NEAR_SHORTCUTS_GROTTO_GRASS_4, RAND_INF_LW_NEAR_SHORTCUTS_GROTTO_GRASS_4 },
    { RC_HF_NEAR_MARKET_GROTTO_GRASS_1, RAND_INF_HF_NEAR_MARKET_GROTTO_GRASS_1 },
    { RC_HF_NEAR_MARKET_GROTTO_GRASS_2, RAND_INF_HF_NEAR_MARKET_GROTTO_GRASS_2 },
    { RC_HF_NEAR_MARKET_GROTTO_GRASS_3, RAND_INF_HF_NEAR_MARKET_GROTTO_GRASS_3 },
    { RC_HF_NEAR_MARKET_GROTTO_GRASS_4, RAND_INF_HF_NEAR_MARKET_GROTTO_GRASS_4 },
    { RC_HF_OPEN_GROTTO_GRASS_1, RAND_INF_HF_OPEN_GROTTO_GRASS_1 },
    { RC_HF_OPEN_GROTTO_GRASS_2, RAND_INF_HF_OPEN_GROTTO_GRASS_2 },
    { RC_HF_OPEN_GROTTO_GRASS_3, RAND_INF_HF_OPEN_GROTTO_GRASS_3 },
    { RC_HF_OPEN_GROTTO_GRASS_4, RAND_INF_HF_OPEN_GROTTO_GRASS_4 },
    { RC_HF_SOUTHEAST_GROTTO_GRASS_1, RAND_INF_HF_SOUTHEAST_GROTTO_GRASS_1 },
    { RC_HF_SOUTHEAST_GROTTO_GRASS_2, RAND_INF_HF_SOUTHEAST_GROTTO_GRASS_2 },
    { RC_HF_SOUTHEAST_GROTTO_GRASS_3, RAND_INF_HF_SOUTHEAST_GROTTO_GRASS_3 },
    { RC_HF_SOUTHEAST_GROTTO_GRASS_4, RAND_INF_HF_SOUTHEAST_GROTTO_GRASS_4 },
    { RC_HF_COW_GROTTO_GRASS_1, RAND_INF_HF_COW_GROTTO_GRASS_1 },
    { RC_HF_COW_GROTTO_GRASS_2, RAND_INF_HF_COW_GROTTO_GRASS_2 },
    { RC_KAK_OPEN_GROTTO_GRASS_1, RAND_INF_KAK_OPEN_GROTTO_GRASS_1 },
    { RC_KAK_OPEN_GROTTO_GRASS_2, RAND_INF_KAK_OPEN_GROTTO_GRASS_2 },
    { RC_KAK_OPEN_GROTTO_GRASS_3, RAND_INF_KAK_OPEN_GROTTO_GRASS_3 },
    { RC_KAK_OPEN_GROTTO_GRASS_4, RAND_INF_KAK_OPEN_GROTTO_GRASS_4 },
    { RC_DMT_STORMS_GROTTO_GRASS_1, RAND_INF_DMT_STORMS_GROTTO_GRASS_1 },
    { RC_DMT_STORMS_GROTTO_GRASS_2, RAND_INF_DMT_STORMS_GROTTO_GRASS_2 },
    { RC_DMT_STORMS_GROTTO_GRASS_3, RAND_INF_DMT_STORMS_GROTTO_GRASS_3 },
    { RC_DMT_STORMS_GROTTO_GRASS_4, RAND_INF_DMT_STORMS_GROTTO_GRASS_4 },
    { RC_DMT_COW_GROTTO_GRASS_1, RAND_INF_DMT_COW_GROTTO_GRASS_1 },
    { RC_DMT_COW_GROTTO_GRASS_2, RAND_INF_DMT_COW_GROTTO_GRASS_2 },
    { RC_DMC_UPPER_GROTTO_GRASS_1, RAND_INF_DMC_UPPER_GROTTO_GRASS_1 },
    { RC_DMC_UPPER_GROTTO_GRASS_2, RAND_INF_DMC_UPPER_GROTTO_GRASS_2 },
    { RC_DMC_UPPER_GROTTO_GRASS_3, RAND_INF_DMC_UPPER_GROTTO_GRASS_3 },
    { RC_DMC_UPPER_GROTTO_GRASS_4, RAND_INF_DMC_UPPER_GROTTO_GRASS_4 },
    { RC_ZR_OPEN_GROTTO_GRASS_1, RAND_INF_ZR_OPEN_GROTTO_GRASS_1 },
    { RC_ZR_OPEN_GROTTO_GRASS_2, RAND_INF_ZR_OPEN_GROTTO_GRASS_2 },
    { RC_ZR_OPEN_GROTTO_GRASS_3, RAND_INF_ZR_OPEN_GROTTO_GRASS_3 },
    { RC_ZR_OPEN_GROTTO_GRASS_4, RAND_INF_ZR_OPEN_GROTTO_GRASS_4 },
    // Dungeon Grass
    { RC_DEKU_TREE_LOBBY_GRASS_1, RAND_INF_DEKU_TREE_LOBBY_GRASS_1 },
    { RC_DEKU_TREE_LOBBY_GRASS_2, RAND_INF_DEKU_TREE_LOBBY_GRASS_2 },
    { RC_DEKU_TREE_LOBBY_GRASS_3, RAND_INF_DEKU_TREE_LOBBY_GRASS_3 },
    { RC_DEKU_TREE_2F_GRASS_1, RAND_INF_DEKU_TREE_2F_GRASS_1 },
    { RC_DEKU_TREE_2F_GRASS_2, RAND_INF_DEKU_TREE_2F_GRASS_2 },
    { RC_DEKU_TREE_SLINGSHOT_GRASS_1, RAND_INF_DEKU_TREE_SLINGSHOT_GRASS_1 },
    { RC_DEKU_TREE_SLINGSHOT_GRASS_2, RAND_INF_DEKU_TREE_SLINGSHOT_GRASS_2 },
    { RC_DEKU_TREE_SLINGSHOT_GRASS_3, RAND_INF_DEKU_TREE_SLINGSHOT_GRASS_3 },
    { RC_DEKU_TREE_SLINGSHOT_GRASS_4, RAND_INF_DEKU_TREE_SLINGSHOT_GRASS_4 },
    { RC_DEKU_TREE_COMPASS_GRASS_1, RAND_INF_DEKU_TREE_COMPASS_GRASS_1 },
    { RC_DEKU_TREE_COMPASS_GRASS_2, RAND_INF_DEKU_TREE_COMPASS_GRASS_2 },
    { RC_DEKU_TREE_BASEMENT_GRASS_1, RAND_INF_DEKU_TREE_BASEMENT_GRASS_1 },
    { RC_DEKU_TREE_BASEMENT_GRASS_2, RAND_INF_DEKU_TREE_BASEMENT_GRASS_2 },
    { RC_DEKU_TREE_BASEMENT_SCRUB_ROOM_GRASS_1, RAND_INF_DEKU_TREE_BASEMENT_SCRUB_ROOM_GRASS_1 },
    { RC_DEKU_TREE_BASEMENT_SCRUB_ROOM_GRASS_2, RAND_INF_DEKU_TREE_BASEMENT_SCRUB_ROOM_GRASS_2 },
    { RC_DEKU_TREE_BASEMENT_SCRUB_ROOM_GRASS_3, RAND_INF_DEKU_TREE_BASEMENT_SCRUB_ROOM_GRASS_3 },
    { RC_DEKU_TREE_BASEMENT_SCRUB_ROOM_GRASS_4, RAND_INF_DEKU_TREE_BASEMENT_SCRUB_ROOM_GRASS_4 },
    { RC_DEKU_TREE_BASEMENT_SPIKE_ROLLER_GRASS_1, RAND_INF_DEKU_TREE_BASEMENT_SPIKE_ROLLER_GRASS_1 },
    { RC_DEKU_TREE_BASEMENT_SPIKE_ROLLER_GRASS_2, RAND_INF_DEKU_TREE_BASEMENT_SPIKE_ROLLER_GRASS_2 },
    { RC_DEKU_TREE_BASEMENT_TORCHES_GRASS_1, RAND_INF_DEKU_TREE_BASEMENT_TORCHES_GRASS_1 },
    { RC_DEKU_TREE_BASEMENT_TORCHES_GRASS_2, RAND_INF_DEKU_TREE_BASEMENT_TORCHES_GRASS_2 },
    { RC_DEKU_TREE_BASEMENT_LARVAE_GRASS_1, RAND_INF_DEKU_TREE_BASEMENT_LARVAE_GRASS_1 },
    { RC_DEKU_TREE_BASEMENT_LARVAE_GRASS_2, RAND_INF_DEKU_TREE_BASEMENT_LARVAE_GRASS_2 },
    { RC_DEKU_TREE_BEFORE_BOSS_GRASS_1, RAND_INF_DEKU_TREE_BEFORE_BOSS_GRASS_1 },
    { RC_DEKU_TREE_BEFORE_BOSS_GRASS_2, RAND_INF_DEKU_TREE_BEFORE_BOSS_GRASS_2 },
    { RC_DEKU_TREE_BEFORE_BOSS_GRASS_3, RAND_INF_DEKU_TREE_BEFORE_BOSS_GRASS_3 },
    { RC_DODONGOS_CAVERN_FIRST_BRIDGE_GRASS, RAND_INF_DODONGOS_CAVERN_FIRST_BRIDGE_GRASS },
    { RC_DODONGOS_CAVERN_BLADE_GRASS, RAND_INF_DODONGOS_CAVERN_BLADE_GRASS },
    { RC_DODONGOS_CAVERN_SINGLE_EYE_GRASS, RAND_INF_DODONGOS_CAVERN_SINGLE_EYE_GRASS },
    { RC_DODONGOS_CAVERN_BEFORE_BOSS_GRASS, RAND_INF_DODONGOS_CAVERN_BEFORE_BOSS_GRASS },
    { RC_BOTTOM_OF_THE_WELL_BASEMENT_BEHIND_ROCKS_GRASS_1, RAND_INF_BOTTOM_OF_THE_WELL_BASEMENT_BEHIND_ROCKS_GRASS_1 },
    { RC_BOTTOM_OF_THE_WELL_BASEMENT_BEHIND_ROCKS_GRASS_2, RAND_INF_BOTTOM_OF_THE_WELL_BASEMENT_BEHIND_ROCKS_GRASS_2 },
    { RC_BOTTOM_OF_THE_WELL_BASEMENT_BEHIND_ROCKS_GRASS_3, RAND_INF_BOTTOM_OF_THE_WELL_BASEMENT_BEHIND_ROCKS_GRASS_3 },
    { RC_BOTTOM_OF_THE_WELL_BASEMENT_BEHIND_ROCKS_GRASS_4, RAND_INF_BOTTOM_OF_THE_WELL_BASEMENT_BEHIND_ROCKS_GRASS_4 },
    { RC_BOTTOM_OF_THE_WELL_BASEMENT_BEHIND_ROCKS_GRASS_5, RAND_INF_BOTTOM_OF_THE_WELL_BASEMENT_BEHIND_ROCKS_GRASS_5 },
    { RC_BOTTOM_OF_THE_WELL_BASEMENT_BEHIND_ROCKS_GRASS_6, RAND_INF_BOTTOM_OF_THE_WELL_BASEMENT_BEHIND_ROCKS_GRASS_6 },
    { RC_BOTTOM_OF_THE_WELL_BASEMENT_BEHIND_ROCKS_GRASS_7, RAND_INF_BOTTOM_OF_THE_WELL_BASEMENT_BEHIND_ROCKS_GRASS_7 },
    { RC_BOTTOM_OF_THE_WELL_BASEMENT_BEHIND_ROCKS_GRASS_8, RAND_INF_BOTTOM_OF_THE_WELL_BASEMENT_BEHIND_ROCKS_GRASS_8 },
    { RC_BOTTOM_OF_THE_WELL_BASEMENT_BEHIND_ROCKS_GRASS_9, RAND_INF_BOTTOM_OF_THE_WELL_BASEMENT_BEHIND_ROCKS_GRASS_9 },
    { RC_BOTTOM_OF_THE_WELL_BASEMENT_GRASS_1, RAND_INF_BOTTOM_OF_THE_WELL_BASEMENT_GRASS_1 },
    { RC_BOTTOM_OF_THE_WELL_BASEMENT_GRASS_2, RAND_INF_BOTTOM_OF_THE_WELL_BASEMENT_GRASS_2 },
    { RC_BOTTOM_OF_THE_WELL_BASEMENT_GRASS_3, RAND_INF_BOTTOM_OF_THE_WELL_BASEMENT_GRASS_3 },
    // MQ Dungeon Grass
    { RC_DEKU_TREE_MQ_LOBBY_GRASS_1, RAND_INF_DEKU_TREE_MQ_LOBBY_GRASS_1 },
    { RC_DEKU_TREE_MQ_LOBBY_GRASS_2, RAND_INF_DEKU_TREE_MQ_LOBBY_GRASS_2 },
    { RC_DEKU_TREE_MQ_LOBBY_GRASS_3, RAND_INF_DEKU_TREE_MQ_LOBBY_GRASS_3 },
    { RC_DEKU_TREE_MQ_LOBBY_GRASS_4, RAND_INF_DEKU_TREE_MQ_LOBBY_GRASS_4 },
    { RC_DEKU_TREE_MQ_LOBBY_GRASS_5, RAND_INF_DEKU_TREE_MQ_LOBBY_GRASS_5 },
    { RC_DEKU_TREE_MQ_2F_GRASS_1, RAND_INF_DEKU_TREE_MQ_2F_GRASS_1 },
    { RC_DEKU_TREE_MQ_2F_GRASS_2, RAND_INF_DEKU_TREE_MQ_2F_GRASS_2 },
    { RC_DEKU_TREE_MQ_SLINGSHOT_GRASS_1, RAND_INF_DEKU_TREE_MQ_SLINGSHOT_GRASS_1 },
    { RC_DEKU_TREE_MQ_SLINGSHOT_GRASS_2, RAND_INF_DEKU_TREE_MQ_SLINGSHOT_GRASS_2 },
    { RC_DEKU_TREE_MQ_SLINGSHOT_GRASS_3, RAND_INF_DEKU_TREE_MQ_SLINGSHOT_GRASS_3 },
    { RC_DEKU_TREE_MQ_SLINGSHOT_GRASS_4, RAND_INF_DEKU_TREE_MQ_SLINGSHOT_GRASS_4 },
    { RC_DEKU_TREE_MQ_BEFORE_COMPASS_GRASS_1, RAND_INF_DEKU_TREE_MQ_BEFORE_COMPASS_GRASS_1 },
    { RC_DEKU_TREE_MQ_BEFORE_COMPASS_GRASS_2, RAND_INF_DEKU_TREE_MQ_BEFORE_COMPASS_GRASS_2 },
    { RC_DEKU_TREE_MQ_BEFORE_COMPASS_GRASS_3, RAND_INF_DEKU_TREE_MQ_BEFORE_COMPASS_GRASS_3 },
    { RC_DEKU_TREE_MQ_BEFORE_COMPASS_GRASS_4, RAND_INF_DEKU_TREE_MQ_BEFORE_COMPASS_GRASS_4 },
    { RC_DEKU_TREE_MQ_BEFORE_COMPASS_GRASS_5, RAND_INF_DEKU_TREE_MQ_BEFORE_COMPASS_GRASS_5 },
    { RC_DEKU_TREE_MQ_BEFORE_COMPASS_GRASS_6, RAND_INF_DEKU_TREE_MQ_BEFORE_COMPASS_GRASS_6 },
    { RC_DEKU_TREE_MQ_BEFORE_COMPASS_GRASS_7, RAND_INF_DEKU_TREE_MQ_BEFORE_COMPASS_GRASS_7 },
    { RC_DEKU_TREE_MQ_COMPASS_GRASS_1, RAND_INF_DEKU_TREE_MQ_COMPASS_GRASS_1 },
    { RC_DEKU_TREE_MQ_COMPASS_GRASS_2, RAND_INF_DEKU_TREE_MQ_COMPASS_GRASS_2 },
    { RC_DEKU_TREE_MQ_COMPASS_GRASS_3, RAND_INF_DEKU_TREE_MQ_COMPASS_GRASS_3 },
    { RC_DEKU_TREE_MQ_COMPASS_GRASS_4, RAND_INF_DEKU_TREE_MQ_COMPASS_GRASS_4 },
    { RC_DEKU_TREE_MQ_BASEMENT_LOWER_GRASS_1, RAND_INF_DEKU_TREE_MQ_BASEMENT_LOWER_GRASS_1 },
    { RC_DEKU_TREE_MQ_BASEMENT_LOWER_GRASS_2, RAND_INF_DEKU_TREE_MQ_BASEMENT_LOWER_GRASS_2 },
    { RC_DEKU_TREE_MQ_BASEMENT_LOWER_GRASS_3, RAND_INF_DEKU_TREE_MQ_BASEMENT_LOWER_GRASS_3 },
    { RC_DEKU_TREE_MQ_BASEMENT_LOWER_GRASS_4, RAND_INF_DEKU_TREE_MQ_BASEMENT_LOWER_GRASS_4 },
    { RC_DEKU_TREE_MQ_BASEMENT_UPPER_GRASS_1, RAND_INF_DEKU_TREE_MQ_BASEMENT_UPPER_GRASS_1 },
    { RC_DEKU_TREE_MQ_BASEMENT_UPPER_GRASS_2, RAND_INF_DEKU_TREE_MQ_BASEMENT_UPPER_GRASS_2 },
    { RC_DEKU_TREE_MQ_BASEMENT_UPPER_GRASS_3, RAND_INF_DEKU_TREE_MQ_BASEMENT_UPPER_GRASS_3 },
    { RC_DEKU_TREE_MQ_BASEMENT_SPIKE_ROLLER_FRONT_GRASS_1, RAND_INF_DEKU_TREE_MQ_BASEMENT_SPIKE_ROLLER_FRONT_GRASS_1 },
    { RC_DEKU_TREE_MQ_BASEMENT_SPIKE_ROLLER_FRONT_GRASS_2, RAND_INF_DEKU_TREE_MQ_BASEMENT_SPIKE_ROLLER_FRONT_GRASS_2 },
    { RC_DEKU_TREE_MQ_BASEMENT_SPIKE_ROLLER_FRONT_GRASS_3, RAND_INF_DEKU_TREE_MQ_BASEMENT_SPIKE_ROLLER_FRONT_GRASS_3 },
    { RC_DEKU_TREE_MQ_BASEMENT_SPIKE_ROLLER_BACK_GRASS_1, RAND_INF_DEKU_TREE_MQ_BASEMENT_SPIKE_ROLLER_BACK_GRASS_1 },
    { RC_DEKU_TREE_MQ_BASEMENT_SPIKE_ROLLER_BACK_GRASS_2, RAND_INF_DEKU_TREE_MQ_BASEMENT_SPIKE_ROLLER_BACK_GRASS_2 },
    { RC_DEKU_TREE_MQ_BASEMENT_TORCHES_GRASS_1, RAND_INF_DEKU_TREE_MQ_BASEMENT_TORCHES_GRASS_1 },
    { RC_DEKU_TREE_MQ_BASEMENT_TORCHES_GRASS_2, RAND_INF_DEKU_TREE_MQ_BASEMENT_TORCHES_GRASS_2 },
    { RC_DEKU_TREE_MQ_BASEMENT_TORCHES_GRASS_3, RAND_INF_DEKU_TREE_MQ_BASEMENT_TORCHES_GRASS_3 },
    { RC_DEKU_TREE_MQ_BASEMENT_TORCHES_GRASS_4, RAND_INF_DEKU_TREE_MQ_BASEMENT_TORCHES_GRASS_4 },
    { RC_DEKU_TREE_MQ_BASEMENT_LARVAE_GRASS_1, RAND_INF_DEKU_TREE_MQ_BASEMENT_LARVAE_GRASS_1 },
    { RC_DEKU_TREE_MQ_BASEMENT_LARVAE_GRASS_2, RAND_INF_DEKU_TREE_MQ_BASEMENT_LARVAE_GRASS_2 },
    { RC_DEKU_TREE_MQ_BASEMENT_GRAVES_GRASS_1, RAND_INF_DEKU_TREE_MQ_BASEMENT_GRAVES_GRASS_1 },
    { RC_DEKU_TREE_MQ_BASEMENT_GRAVES_GRASS_2, RAND_INF_DEKU_TREE_MQ_BASEMENT_GRAVES_GRASS_2 },
    { RC_DEKU_TREE_MQ_BASEMENT_GRAVES_GRASS_3, RAND_INF_DEKU_TREE_MQ_BASEMENT_GRAVES_GRASS_3 },
    { RC_DEKU_TREE_MQ_BASEMENT_GRAVES_GRASS_4, RAND_INF_DEKU_TREE_MQ_BASEMENT_GRAVES_GRASS_4 },
    { RC_DEKU_TREE_MQ_BASEMENT_GRAVES_GRASS_5, RAND_INF_DEKU_TREE_MQ_BASEMENT_GRAVES_GRASS_5 },
    { RC_DEKU_TREE_MQ_BASEMENT_BACK_GRASS_1, RAND_INF_DEKU_TREE_MQ_BASEMENT_BACK_GRASS_1 },
    { RC_DEKU_TREE_MQ_BASEMENT_BACK_GRASS_2, RAND_INF_DEKU_TREE_MQ_BASEMENT_BACK_GRASS_2 },
    { RC_DEKU_TREE_MQ_BASEMENT_BACK_GRASS_3, RAND_INF_DEKU_TREE_MQ_BASEMENT_BACK_GRASS_3 },
    { RC_DEKU_TREE_MQ_BEFORE_BOSS_GRASS_1, RAND_INF_DEKU_TREE_MQ_BEFORE_BOSS_GRASS_1 },
    { RC_DEKU_TREE_MQ_BEFORE_BOSS_GRASS_2, RAND_INF_DEKU_TREE_MQ_BEFORE_BOSS_GRASS_2 },
    { RC_DEKU_TREE_MQ_BEFORE_BOSS_GRASS_3, RAND_INF_DEKU_TREE_MQ_BEFORE_BOSS_GRASS_3 },
    { RC_DODONGOS_CAVERN_MQ_COMPASS_GRASS_1, RAND_INF_DODONGOS_CAVERN_MQ_COMPASS_GRASS_1 },
    { RC_DODONGOS_CAVERN_MQ_COMPASS_GRASS_2, RAND_INF_DODONGOS_CAVERN_MQ_COMPASS_GRASS_2 },
    { RC_DODONGOS_CAVERN_MQ_COMPASS_GRASS_3, RAND_INF_DODONGOS_CAVERN_MQ_COMPASS_GRASS_3 },
    { RC_DODONGOS_CAVERN_MQ_COMPASS_GRASS_4, RAND_INF_DODONGOS_CAVERN_MQ_COMPASS_GRASS_4 },
    { RC_DODONGOS_CAVERN_MQ_ARMOS_GRASS, RAND_INF_DODONGOS_CAVERN_MQ_ARMOS_GRASS },
    { RC_DODONGOS_CAVERN_MQ_BACK_POE_GRASS, RAND_INF_DODONGOS_CAVERN_MQ_BACK_POE_GRASS },
    { RC_DODONGOS_CAVERN_MQ_SCRUB_GRASS_1, RAND_INF_DODONGOS_CAVERN_MQ_SCRUB_GRASS_1 },
    { RC_DODONGOS_CAVERN_MQ_SCRUB_GRASS_2, RAND_INF_DODONGOS_CAVERN_MQ_SCRUB_GRASS_2 },
    { RC_JABU_JABUS_BELLY_MQ_FIRST_GRASS_1, RAND_INF_JABU_JABUS_BELLY_MQ_FIRST_GRASS_1 },
    { RC_JABU_JABUS_BELLY_MQ_FIRST_GRASS_2, RAND_INF_JABU_JABUS_BELLY_MQ_FIRST_GRASS_2 },
    { RC_JABU_JABUS_BELLY_MQ_PIT_GRASS_1, RAND_INF_JABU_JABUS_BELLY_MQ_PIT_GRASS_1 },
    { RC_JABU_JABUS_BELLY_MQ_PIT_GRASS_2, RAND_INF_JABU_JABUS_BELLY_MQ_PIT_GRASS_2 },
    { RC_JABU_JABUS_BELLY_MQ_BASEMENT_GRASS_1, RAND_INF_JABU_JABUS_BELLY_MQ_BASEMENT_GRASS_1 },
    { RC_JABU_JABUS_BELLY_MQ_BASEMENT_GRASS_2, RAND_INF_JABU_JABUS_BELLY_MQ_BASEMENT_GRASS_2 },
    { RC_JABU_JABUS_BELLY_MQ_BASEMENT_GRASS_3, RAND_INF_JABU_JABUS_BELLY_MQ_BASEMENT_GRASS_3 },
    { RC_JABU_JABUS_BELLY_MQ_JIGGLIES_GRASS, RAND_INF_JABU_JABUS_BELLY_MQ_JIGGLIES_GRASS },
    { RC_JABU_JABUS_BELLY_MQ_AFTER_BIG_OCTO_GRASS_1, RAND_INF_JABU_JABUS_BELLY_MQ_AFTER_BIG_OCTO_GRASS_1 },
    { RC_JABU_JABUS_BELLY_MQ_AFTER_BIG_OCTO_GRASS_2, RAND_INF_JABU_JABUS_BELLY_MQ_AFTER_BIG_OCTO_GRASS_2 },
    { RC_JABU_JABUS_BELLY_MQ_FALLING_LIKE_LIKE_GRASS, RAND_INF_JABU_JABUS_BELLY_MQ_FALLING_LIKE_LIKE_GRASS },
    { RC_JABU_JABUS_BELLY_MQ_BASEMENT_BOOMERANG_GRASS, RAND_INF_JABU_JABUS_BELLY_MQ_BASEMENT_BOOMERANG_GRASS },
    { RC_JABU_JABUS_BELLY_MQ_BEFORE_BOSS_GRASS_1, RAND_INF_JABU_JABUS_BELLY_MQ_BEFORE_BOSS_GRASS_1 },
    { RC_JABU_JABUS_BELLY_MQ_BEFORE_BOSS_GRASS_2, RAND_INF_JABU_JABUS_BELLY_MQ_BEFORE_BOSS_GRASS_2 },
    { RC_BOTTOM_OF_THE_WELL_MQ_DEAD_HAND_GRASS_1, RAND_INF_BOTTOM_OF_THE_WELL_MQ_DEAD_HAND_GRASS_1 },
    { RC_BOTTOM_OF_THE_WELL_MQ_DEAD_HAND_GRASS_2, RAND_INF_BOTTOM_OF_THE_WELL_MQ_DEAD_HAND_GRASS_2 },
    { RC_BOTTOM_OF_THE_WELL_MQ_DEAD_HAND_GRASS_3, RAND_INF_BOTTOM_OF_THE_WELL_MQ_DEAD_HAND_GRASS_3 },
    { RC_BOTTOM_OF_THE_WELL_MQ_DEAD_HAND_GRASS_4, RAND_INF_BOTTOM_OF_THE_WELL_MQ_DEAD_HAND_GRASS_4 },
    // Shared Dungeon Grass
    { RC_DEKU_TREE_QUEEN_GOHMA_GRASS_1, RAND_INF_DEKU_TREE_QUEEN_GOHMA_GRASS_1 },
    { RC_DEKU_TREE_QUEEN_GOHMA_GRASS_2, RAND_INF_DEKU_TREE_QUEEN_GOHMA_GRASS_2 },
    { RC_DEKU_TREE_QUEEN_GOHMA_GRASS_3, RAND_INF_DEKU_TREE_QUEEN_GOHMA_GRASS_3 },
    { RC_DEKU_TREE_QUEEN_GOHMA_GRASS_4, RAND_INF_DEKU_TREE_QUEEN_GOHMA_GRASS_4 },
    { RC_DEKU_TREE_QUEEN_GOHMA_GRASS_5, RAND_INF_DEKU_TREE_QUEEN_GOHMA_GRASS_5 },
    { RC_DEKU_TREE_QUEEN_GOHMA_GRASS_6, RAND_INF_DEKU_TREE_QUEEN_GOHMA_GRASS_6 },
    { RC_DEKU_TREE_QUEEN_GOHMA_GRASS_7, RAND_INF_DEKU_TREE_QUEEN_GOHMA_GRASS_7 },
    { RC_DEKU_TREE_QUEEN_GOHMA_GRASS_8, RAND_INF_DEKU_TREE_QUEEN_GOHMA_GRASS_8 },
    // End Grass

    { RC_KF_LINKS_HOUSE_POT, RAND_INF_KF_LINKS_HOUSE_POT },
    { RC_KF_TWINS_HOUSE_POT_1, RAND_INF_KF_TWINS_HOUSE_POT_1 },
    { RC_KF_TWINS_HOUSE_POT_2, RAND_INF_KF_TWINS_HOUSE_POT_2 },
    { RC_KF_BROTHERS_HOUSE_POT_1, RAND_INF_KF_BROTHERS_HOUSE_POT_1 },
    { RC_KF_BROTHERS_HOUSE_POT_2, RAND_INF_KF_BROTHERS_HOUSE_POT_2 },
    { RC_TH_BREAK_ROOM_FRONT_POT, RAND_INF_TH_BREAK_ROOM_FRONT_POT },
    { RC_TH_BREAK_ROOM_BACK_POT, RAND_INF_TH_BREAK_ROOM_BACK_POT },
    { RC_TH_KITCHEN_POT_1, RAND_INF_TH_KITCHEN_POT_1 },
    { RC_TH_KITCHEN_POT_2, RAND_INF_TH_KITCHEN_POT_2 },
    { RC_TH_1_TORCH_CELL_RIGHT_POT, RAND_INF_TH_1_TORCH_CELL_RIGHT_POT },
    { RC_TH_1_TORCH_CELL_MID_POT, RAND_INF_TH_1_TORCH_CELL_MID_POT },
    { RC_TH_1_TORCH_CELL_LEFT_POT, RAND_INF_TH_1_TORCH_CELL_LEFT_POT },
    { RC_TH_STEEP_SLOPE_RIGHT_POT, RAND_INF_TH_STEEP_SLOPE_RIGHT_POT },
    { RC_TH_STEEP_SLOPE_LEFT_POT, RAND_INF_TH_STEEP_SLOPE_LEFT_POT },
    { RC_TH_NEAR_DOUBLE_CELL_RIGHT_POT, RAND_INF_TH_NEAR_DOUBLE_CELL_RIGHT_POT },
    { RC_TH_NEAR_DOUBLE_CELL_MID_POT, RAND_INF_TH_NEAR_DOUBLE_CELL_MID_POT },
    { RC_TH_NEAR_DOUBLE_CELL_LEFT_POT, RAND_INF_NEAR_DOUBLE_CELL_LEFT_POT },
    { RC_TH_RIGHTMOST_JAILED_POT, RAND_INF_TH_RIGHTMOST_JAILED_POT },
    { RC_TH_RIGHT_MIDDLE_JAILED_POT, RAND_INF_TH_RIGHT_MIDDLE_JAILED_POT },
    { RC_TH_LEFT_MIDDLE_JAILED_POT, RAND_INF_TH_LEFT_MIDDLE_JAILED_POT },
    { RC_TH_LEFTMOST_JAILED_POT, RAND_INF_TH_LEFTMOST_JAILED_POT },
    { RC_WASTELAND_NEAR_GS_POT_1, RAND_INF_WASTELAND_NEAR_GS_POT_1 },
    { RC_WASTELAND_NEAR_GS_POT_2, RAND_INF_WASTELAND_NEAR_GS_POT_2 },
    { RC_WASTELAND_NEAR_GS_POT_3, RAND_INF_WASTELAND_NEAR_GS_POT_3 },
    { RC_WASTELAND_NEAR_GS_POT_4, RAND_INF_WASTELAND_NEAR_GS_POT_4 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_1, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_1 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_2, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_2 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_3, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_3 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_4, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_4 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_5, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_5 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_6, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_6 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_7, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_7 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_8, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_8 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_9, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_9 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_10, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_10 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_11, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_11 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_12, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_12 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_13, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_13 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_14, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_14 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_15, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_15 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_16, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_16 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_17, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_17 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_18, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_18 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_19, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_19 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_20, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_20 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_21, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_21 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_22, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_22 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_23, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_23 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_24, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_24 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_25, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_25 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_26, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_26 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_27, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_27 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_28, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_28 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_29, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_29 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_30, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_30 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_31, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_31 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_32, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_32 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_33, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_33 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_34, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_34 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_35, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_35 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_36, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_36 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_37, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_37 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_38, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_38 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_39, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_39 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_40, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_40 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_41, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_41 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_42, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_42 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_43, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_43 },
    { RC_MK_GUARD_HOUSE_CHILD_POT_44, RAND_INF_MK_GUARD_HOUSE_CHILD_POT_44 },
    { RC_MK_GUARD_HOUSE_ADULT_POT_1, RAND_INF_MK_GUARD_HOUSE_ADULT_POT_1 },
    { RC_MK_GUARD_HOUSE_ADULT_POT_2, RAND_INF_MK_GUARD_HOUSE_ADULT_POT_2 },
    { RC_MK_GUARD_HOUSE_ADULT_POT_3, RAND_INF_MK_GUARD_HOUSE_ADULT_POT_3 },
    { RC_MK_GUARD_HOUSE_ADULT_POT_4, RAND_INF_MK_GUARD_HOUSE_ADULT_POT_4 },
    { RC_MK_GUARD_HOUSE_ADULT_POT_5, RAND_INF_MK_GUARD_HOUSE_ADULT_POT_5 },
    { RC_MK_GUARD_HOUSE_ADULT_POT_6, RAND_INF_MK_GUARD_HOUSE_ADULT_POT_6 },
    { RC_MK_GUARD_HOUSE_ADULT_POT_7, RAND_INF_MK_GUARD_HOUSE_ADULT_POT_7 },
    { RC_MK_GUARD_HOUSE_ADULT_POT_8, RAND_INF_MK_GUARD_HOUSE_ADULT_POT_8 },
    { RC_MK_GUARD_HOUSE_ADULT_POT_9, RAND_INF_MK_GUARD_HOUSE_ADULT_POT_9 },
    { RC_MK_GUARD_HOUSE_ADULT_POT_10, RAND_INF_MK_GUARD_HOUSE_ADULT_POT_10 },
    { RC_MK_GUARD_HOUSE_ADULT_POT_11, RAND_INF_MK_GUARD_HOUSE_ADULT_POT_11 },
    { RC_MK_BACK_ALLEY_HOUSE_POT_1, RAND_INF_MK_BACK_ALLEY_HOUSE_POT_1 },
    { RC_MK_BACK_ALLEY_HOUSE_POT_2, RAND_INF_MK_BACK_ALLEY_HOUSE_POT_2 },
    { RC_MK_BACK_ALLEY_HOUSE_POT_3, RAND_INF_MK_BACK_ALLEY_HOUSE_POT_3 },
    { RC_KAK_NEAR_POTION_SHOP_POT_1, RAND_INF_KAK_NEAR_POTION_SHOP_POT_1 },
    { RC_KAK_NEAR_POTION_SHOP_POT_2, RAND_INF_KAK_NEAR_POTION_SHOP_POT_2 },
    { RC_KAK_NEAR_POTION_SHOP_POT_3, RAND_INF_KAK_NEAR_POTION_SHOP_POT_3 },
    { RC_KAK_NEAR_IMPAS_HOUSE_POT_1, RAND_INF_KAK_NEAR_IMPAS_HOUSE_POT_1 },
    { RC_KAK_NEAR_IMPAS_HOUSE_POT_2, RAND_INF_KAK_NEAR_IMPAS_HOUSE_POT_2 },
    { RC_KAK_NEAR_IMPAS_HOUSE_POT_3, RAND_INF_KAK_NEAR_IMPAS_HOUSE_POT_3 },
    { RC_KAK_NEAR_GUARDS_HOUSE_POT_1, RAND_INF_KAK_NEAR_GUARDS_HOUSE_POT_1 },
    { RC_KAK_NEAR_GUARDS_HOUSE_POT_2, RAND_INF_KAK_NEAR_GUARDS_HOUSE_POT_2 },
    { RC_KAK_NEAR_GUARDS_HOUSE_POT_3, RAND_INF_KAK_NEAR_GUARDS_HOUSE_POT_3 },
    { RC_KAK_NEAR_MEDICINE_SHOP_POT_1, RAND_INF_KAK_NEAR_MEDICINE_SHOP_POT_1 },
    { RC_KAK_NEAR_MEDICINE_SHOP_POT_2, RAND_INF_KAK_NEAR_MEDICINE_SHOP_POT_2 },
    { RC_GY_DAMPES_GRAVE_POT_1, RAND_INF_GY_DAMPES_GRAVE_POT_1 },
    { RC_GY_DAMPES_GRAVE_POT_2, RAND_INF_GY_DAMPES_GRAVE_POT_2 },
    { RC_GY_DAMPES_GRAVE_POT_3, RAND_INF_GY_DAMPES_GRAVE_POT_3 },
    { RC_GY_DAMPES_GRAVE_POT_4, RAND_INF_GY_DAMPES_GRAVE_POT_4 },
    { RC_GY_DAMPES_GRAVE_POT_5, RAND_INF_GY_DAMPES_GRAVE_POT_5 },
    { RC_GY_DAMPES_GRAVE_POT_6, RAND_INF_GY_DAMPES_GRAVE_POT_6 },
    { RC_GC_LOWER_STAIRCASE_POT_1, RAND_INF_GC_LOWER_STAIRCASE_POT_1 },
    { RC_GC_LOWER_STAIRCASE_POT_2, RAND_INF_GC_LOWER_STAIRCASE_POT_2 },
    { RC_GC_UPPER_STAIRCASE_POT_1, RAND_INF_GC_UPPER_STAIRCASE_POT_1 },
    { RC_GC_UPPER_STAIRCASE_POT_2, RAND_INF_GC_UPPER_STAIRCASE_POT_2 },
    { RC_GC_UPPER_STAIRCASE_POT_3, RAND_INF_GC_UPPER_STAIRCASE_POT_3 },
    { RC_GC_MEDIGORON_POT_1, RAND_INF_GC_MEDIGORON_POT_1 },
    { RC_GC_DARUNIA_POT_1, RAND_INF_GC_DARUNIA_POT_1 },
    { RC_GC_DARUNIA_POT_2, RAND_INF_GC_DARUNIA_POT_2 },
    { RC_GC_DARUNIA_POT_3, RAND_INF_GC_DARUNIA_POT_3 },
    { RC_DMC_NEAR_GC_POT_1, RAND_INF_DMC_NEAR_GC_POT_1 },
    { RC_DMC_NEAR_GC_POT_2, RAND_INF_DMC_NEAR_GC_POT_2 },
    { RC_DMC_NEAR_GC_POT_3, RAND_INF_DMC_NEAR_GC_POT_3 },
    { RC_DMC_NEAR_GC_POT_4, RAND_INF_DMC_NEAR_GC_POT_4 },
    { RC_ZD_NEAR_SHOP_POT_1, RAND_INF_ZD_NEAR_SHOP_POT_1 },
    { RC_ZD_NEAR_SHOP_POT_2, RAND_INF_ZD_NEAR_SHOP_POT_2 },
    { RC_ZD_NEAR_SHOP_POT_3, RAND_INF_ZD_NEAR_SHOP_POT_3 },
    { RC_ZD_NEAR_SHOP_POT_4, RAND_INF_ZD_NEAR_SHOP_POT_4 },
    { RC_ZD_NEAR_SHOP_POT_5, RAND_INF_ZD_NEAR_SHOP_POT_5 },
    { RC_ZF_HIDDEN_CAVE_POT_1, RAND_INF_ZF_HIDDEN_CAVE_POT_1 },
    { RC_ZF_HIDDEN_CAVE_POT_2, RAND_INF_ZF_HIDDEN_CAVE_POT_2 },
    { RC_ZF_HIDDEN_CAVE_POT_3, RAND_INF_ZF_HIDDEN_CAVE_POT_3 },
    { RC_ZF_NEAR_JABU_POT_1, RAND_INF_ZF_NEAR_JABU_POT_1 },
    { RC_ZF_NEAR_JABU_POT_2, RAND_INF_ZF_NEAR_JABU_POT_2 },
    { RC_ZF_NEAR_JABU_POT_3, RAND_INF_ZF_NEAR_JABU_POT_3 },
    { RC_ZF_NEAR_JABU_POT_4, RAND_INF_ZF_NEAR_JABU_POT_4 },
    { RC_LLR_FRONT_POT_1, RAND_INF_LLR_FRONT_POT_1 },
    { RC_LLR_FRONT_POT_2, RAND_INF_LLR_FRONT_POT_2 },
    { RC_LLR_FRONT_POT_3, RAND_INF_LLR_FRONT_POT_3 },
    { RC_LLR_FRONT_POT_4, RAND_INF_LLR_FRONT_POT_4 },
    { RC_LLR_RAIN_SHED_POT_1, RAND_INF_LLR_RAIN_SHED_POT_1 },
    { RC_LLR_RAIN_SHED_POT_2, RAND_INF_LLR_RAIN_SHED_POT_2 },
    { RC_LLR_RAIN_SHED_POT_3, RAND_INF_LLR_RAIN_SHED_POT_3 },
    { RC_LLR_TALONS_HOUSE_POT_1, RAND_INF_LLR_TALONS_HOUSE_POT_1 },
    { RC_LLR_TALONS_HOUSE_POT_2, RAND_INF_LLR_TALONS_HOUSE_POT_2 },
    { RC_LLR_TALONS_HOUSE_POT_3, RAND_INF_LLR_TALONS_HOUSE_POT_3 },
    { RC_HF_COW_GROTTO_POT_1, RAND_INF_HF_COW_GROTTO_POT_1 },
    { RC_HF_COW_GROTTO_POT_2, RAND_INF_HF_COW_GROTTO_POT_2 },
    { RC_HC_STORMS_GROTTO_POT_1, RAND_INF_HC_STORMS_GROTTO_POT_1 },
    { RC_HC_STORMS_GROTTO_POT_2, RAND_INF_HC_STORMS_GROTTO_POT_2 },
    { RC_HC_STORMS_GROTTO_POT_3, RAND_INF_HC_STORMS_GROTTO_POT_3 },
    { RC_HC_STORMS_GROTTO_POT_4, RAND_INF_HC_STORMS_GROTTO_POT_4 },
    { RC_DODONGOS_CAVERN_LIZALFOS_POT_1, RAND_INF_DODONGOS_CAVERN_LIZALFOS_POT_1 },
    { RC_DODONGOS_CAVERN_LIZALFOS_POT_2, RAND_INF_DODONGOS_CAVERN_LIZALFOS_POT_2 },
    { RC_DODONGOS_CAVERN_LIZALFOS_POT_3, RAND_INF_DODONGOS_CAVERN_LIZALFOS_POT_3 },
    { RC_DODONGOS_CAVERN_LIZALFOS_POT_4, RAND_INF_DODONGOS_CAVERN_LIZALFOS_POT_4 },
    { RC_DODONGOS_CAVERN_SIDE_ROOM_POT_1, RAND_INF_DODONGOS_CAVERN_SIDE_ROOM_POT_1 },
    { RC_DODONGOS_CAVERN_SIDE_ROOM_POT_2, RAND_INF_DODONGOS_CAVERN_SIDE_ROOM_POT_2 },
    { RC_DODONGOS_CAVERN_SIDE_ROOM_POT_3, RAND_INF_DODONGOS_CAVERN_SIDE_ROOM_POT_3 },
    { RC_DODONGOS_CAVERN_SIDE_ROOM_POT_4, RAND_INF_DODONGOS_CAVERN_SIDE_ROOM_POT_4 },
    { RC_DODONGOS_CAVERN_SIDE_ROOM_POT_5, RAND_INF_DODONGOS_CAVERN_SIDE_ROOM_POT_5 },
    { RC_DODONGOS_CAVERN_SIDE_ROOM_POT_6, RAND_INF_DODONGOS_CAVERN_SIDE_ROOM_POT_6 },
    { RC_DODONGOS_CAVERN_TORCH_ROOM_POT_1, RAND_INF_DODONGOS_CAVERN_TORCH_ROOM_POT_1 },
    { RC_DODONGOS_CAVERN_TORCH_ROOM_POT_2, RAND_INF_DODONGOS_CAVERN_TORCH_ROOM_POT_2 },
    { RC_DODONGOS_CAVERN_TORCH_ROOM_POT_3, RAND_INF_DODONGOS_CAVERN_TORCH_ROOM_POT_3 },
    { RC_DODONGOS_CAVERN_TORCH_ROOM_POT_4, RAND_INF_DODONGOS_CAVERN_TORCH_ROOM_POT_4 },
    { RC_DODONGOS_CAVERN_STAIRCASE_POT_1, RAND_INF_DODONGOS_CAVERN_STAIRCASE_POT_1 },
    { RC_DODONGOS_CAVERN_STAIRCASE_POT_2, RAND_INF_DODONGOS_CAVERN_STAIRCASE_POT_2 },
    { RC_DODONGOS_CAVERN_STAIRCASE_POT_3, RAND_INF_DODONGOS_CAVERN_STAIRCASE_POT_3 },
    { RC_DODONGOS_CAVERN_STAIRCASE_POT_4, RAND_INF_DODONGOS_CAVERN_STAIRCASE_POT_4 },
    { RC_DODONGOS_CAVERN_SINGLE_EYE_POT_1, RAND_INF_DODONGOS_CAVERN_SINGLE_EYE_POT_1 },
    { RC_DODONGOS_CAVERN_SINGLE_EYE_POT_2, RAND_INF_DODONGOS_CAVERN_SINGLE_EYE_POT_2 },
    { RC_DODONGOS_CAVERN_BLADE_POT_1, RAND_INF_DODONGOS_CAVERN_BLADE_POT_1 },
    { RC_DODONGOS_CAVERN_BLADE_POT_2, RAND_INF_DODONGOS_CAVERN_BLADE_POT_2 },
    { RC_DODONGOS_CAVERN_DOUBLE_EYE_POT_1, RAND_INF_DODONGOS_CAVERN_DOUBLE_EYE_POT_1 },
    { RC_DODONGOS_CAVERN_DOUBLE_EYE_POT_2, RAND_INF_DODONGOS_CAVERN_DOUBLE_EYE_POT_2 },
    { RC_DODONGOS_CAVERN_BACK_ROOM_POT_1, RAND_INF_DODONGOS_CAVERN_BACK_ROOM_POT_1 },
    { RC_DODONGOS_CAVERN_BACK_ROOM_POT_2, RAND_INF_DODONGOS_CAVERN_BACK_ROOM_POT_2 },
    { RC_DODONGOS_CAVERN_BACK_ROOM_POT_3, RAND_INF_DODONGOS_CAVERN_BACK_ROOM_POT_3 },
    { RC_DODONGOS_CAVERN_BACK_ROOM_POT_4, RAND_INF_DODONGOS_CAVERN_BACK_ROOM_POT_4 },
    { RC_JABU_JABUS_BELLY_ABOVE_BIG_OCTO_POT_1, RAND_INF_JABU_JABUS_BELLY_ABOVE_BIG_OCTO_POT_1 },
    { RC_JABU_JABUS_BELLY_ABOVE_BIG_OCTO_POT_2, RAND_INF_JABU_JABUS_BELLY_ABOVE_BIG_OCTO_POT_2 },
    { RC_JABU_JABUS_BELLY_ABOVE_BIG_OCTO_POT_3, RAND_INF_JABU_JABUS_BELLY_ABOVE_BIG_OCTO_POT_3 },
    { RC_JABU_JABUS_BELLY_BARINADE_POT_1, RAND_INF_JABU_JABUS_BELLY_BARINADE_POT_1 },
    { RC_JABU_JABUS_BELLY_BARINADE_POT_2, RAND_INF_JABU_JABUS_BELLY_BARINADE_POT_2 },
    { RC_JABU_JABUS_BELLY_BARINADE_POT_3, RAND_INF_JABU_JABUS_BELLY_BARINADE_POT_3 },
    { RC_JABU_JABUS_BELLY_BARINADE_POT_4, RAND_INF_JABU_JABUS_BELLY_BARINADE_POT_4 },
    { RC_JABU_JABUS_BELLY_BARINADE_POT_5, RAND_INF_JABU_JABUS_BELLY_BARINADE_POT_5 },
    { RC_JABU_JABUS_BELLY_BARINADE_POT_6, RAND_INF_JABU_JABUS_BELLY_BARINADE_POT_6 },
    { RC_JABU_JABUS_BELLY_BASEMENT_POT_1, RAND_INF_JABU_JABUS_BELLY_BASEMENT_POT_1 },
    { RC_JABU_JABUS_BELLY_BASEMENT_POT_2, RAND_INF_JABU_JABUS_BELLY_BASEMENT_POT_2 },
    { RC_JABU_JABUS_BELLY_BASEMENT_POT_3, RAND_INF_JABU_JABUS_BELLY_BASEMENT_POT_3 },
    { RC_JABU_JABUS_BELLY_TWO_OCTOROK_POT_1, RAND_INF_JABU_JABUS_BELLY_TWO_OCTOROK_POT_1 },
    { RC_JABU_JABUS_BELLY_TWO_OCTOROK_POT_2, RAND_INF_JABU_JABUS_BELLY_TWO_OCTOROK_POT_2 },
    { RC_JABU_JABUS_BELLY_TWO_OCTOROK_POT_3, RAND_INF_JABU_JABUS_BELLY_TWO_OCTOROK_POT_3 },
    { RC_JABU_JABUS_BELLY_TWO_OCTOROK_POT_4, RAND_INF_JABU_JABUS_BELLY_TWO_OCTOROK_POT_4 },
    { RC_JABU_JABUS_BELLY_TWO_OCTOROK_POT_5, RAND_INF_JABU_JABUS_BELLY_TWO_OCTOROK_POT_5 },
    { RC_FOREST_TEMPLE_LOBBY_POT_1, RAND_INF_FOREST_TEMPLE_LOBBY_POT_1 },
    { RC_FOREST_TEMPLE_LOBBY_POT_2, RAND_INF_FOREST_TEMPLE_LOBBY_POT_2 },
    { RC_FOREST_TEMPLE_LOBBY_POT_3, RAND_INF_FOREST_TEMPLE_LOBBY_POT_3 },
    { RC_FOREST_TEMPLE_LOBBY_POT_4, RAND_INF_FOREST_TEMPLE_LOBBY_POT_4 },
    { RC_FOREST_TEMPLE_LOBBY_POT_5, RAND_INF_FOREST_TEMPLE_LOBBY_POT_5 },
    { RC_FOREST_TEMPLE_LOBBY_POT_6, RAND_INF_FOREST_TEMPLE_LOBBY_POT_6 },
    { RC_FOREST_TEMPLE_LOWER_STALFOS_POT_1, RAND_INF_FOREST_TEMPLE_LOWER_STALFOS_POT_1 },
    { RC_FOREST_TEMPLE_LOWER_STALFOS_POT_2, RAND_INF_FOREST_TEMPLE_LOWER_STALFOS_POT_2 },
    { RC_FOREST_TEMPLE_GREEN_POE_POT_1, RAND_INF_FOREST_TEMPLE_GREEN_POE_POT_1 },
    { RC_FOREST_TEMPLE_GREEN_POE_POT_2, RAND_INF_FOREST_TEMPLE_GREEN_POE_POT_2 },
    { RC_FOREST_TEMPLE_UPPER_STALFOS_POT_1, RAND_INF_FOREST_TEMPLE_UPPER_STALFOS_POT_1 },
    { RC_FOREST_TEMPLE_UPPER_STALFOS_POT_2, RAND_INF_FOREST_TEMPLE_UPPER_STALFOS_POT_2 },
    { RC_FOREST_TEMPLE_UPPER_STALFOS_POT_3, RAND_INF_FOREST_TEMPLE_UPPER_STALFOS_POT_3 },
    { RC_FOREST_TEMPLE_UPPER_STALFOS_POT_4, RAND_INF_FOREST_TEMPLE_UPPER_STALFOS_POT_4 },
    { RC_FOREST_TEMPLE_BLUE_POE_POT_1, RAND_INF_FOREST_TEMPLE_BLUE_POE_POT_1 },
    { RC_FOREST_TEMPLE_BLUE_POE_POT_2, RAND_INF_FOREST_TEMPLE_BLUE_POE_POT_2 },
    { RC_FOREST_TEMPLE_BLUE_POE_POT_3, RAND_INF_FOREST_TEMPLE_BLUE_POE_POT_3 },
    { RC_FOREST_TEMPLE_FROZEN_EYE_POT_1, RAND_INF_FOREST_TEMPLE_FROZEN_EYE_POT_1 },
    { RC_FOREST_TEMPLE_FROZEN_EYE_POT_2, RAND_INF_FOREST_TEMPLE_FROZEN_EYE_POT_2 },
    { RC_FIRE_TEMPLE_NEAR_BOSS_POT_1, RAND_INF_FIRE_TEMPLE_NEAR_BOSS_POT_1 },
    { RC_FIRE_TEMPLE_NEAR_BOSS_POT_2, RAND_INF_FIRE_TEMPLE_NEAR_BOSS_POT_2 },
    { RC_FIRE_TEMPLE_NEAR_BOSS_POT_3, RAND_INF_FIRE_TEMPLE_NEAR_BOSS_POT_3 },
    { RC_FIRE_TEMPLE_NEAR_BOSS_POT_4, RAND_INF_FIRE_TEMPLE_NEAR_BOSS_POT_4 },
    { RC_FIRE_TEMPLE_BIG_LAVA_POT_1, RAND_INF_FIRE_TEMPLE_BIG_LAVA_POT_1 },
    { RC_FIRE_TEMPLE_BIG_LAVA_POT_2, RAND_INF_FIRE_TEMPLE_BIG_LAVA_POT_2 },
    { RC_FIRE_TEMPLE_BIG_LAVA_POT_3, RAND_INF_FIRE_TEMPLE_BIG_LAVA_POT_3 },
    { RC_FIRE_TEMPLE_FLAME_MAZE_LEFT_POT_1, RAND_INF_FIRE_TEMPLE_FLAME_MAZE_LEFT_POT_1 },
    { RC_FIRE_TEMPLE_FLAME_MAZE_LEFT_POT_2, RAND_INF_FIRE_TEMPLE_FLAME_MAZE_LEFT_POT_2 },
    { RC_FIRE_TEMPLE_FLAME_MAZE_LEFT_POT_3, RAND_INF_FIRE_TEMPLE_FLAME_MAZE_LEFT_POT_3 },
    { RC_FIRE_TEMPLE_FLAME_MAZE_LEFT_POT_4, RAND_INF_FIRE_TEMPLE_FLAME_MAZE_LEFT_POT_4 },
    { RC_FIRE_TEMPLE_FLAME_MAZE_RIGHT_POT_1, RAND_INF_FIRE_TEMPLE_FLAME_MAZE_RIGHT_POT_1 },
    { RC_FIRE_TEMPLE_FLAME_MAZE_RIGHT_POT_2, RAND_INF_FIRE_TEMPLE_FLAME_MAZE_RIGHT_POT_2 },
    { RC_FIRE_TEMPLE_FLAME_MAZE_RIGHT_POT_3, RAND_INF_FIRE_TEMPLE_FLAME_MAZE_RIGHT_POT_3 },
    { RC_FIRE_TEMPLE_FLAME_MAZE_RIGHT_POT_4, RAND_INF_FIRE_TEMPLE_FLAME_MAZE_RIGHT_POT_4 },
    { RC_WATER_TEMPLE_MAIN_LEVEL_2_POT_1, RAND_INF_WATER_TEMPLE_MAIN_LEVEL_2_POT_1 },
    { RC_WATER_TEMPLE_MAIN_LEVEL_2_POT_2, RAND_INF_WATER_TEMPLE_MAIN_LEVEL_2_POT_2 },
    { RC_WATER_TEMPLE_MAIN_LEVEL_1_POT_1, RAND_INF_WATER_TEMPLE_MAIN_LEVEL_1_POT_1 },
    { RC_WATER_TEMPLE_MAIN_LEVEL_1_POT_2, RAND_INF_WATER_TEMPLE_MAIN_LEVEL_1_POT_2 },
    { RC_WATER_TEMPLE_TORCH_POT_1, RAND_INF_WATER_TEMPLE_TORCH_POT_1 },
    { RC_WATER_TEMPLE_TORCH_POT_2, RAND_INF_WATER_TEMPLE_TORCH_POT_2 },
    { RC_WATER_TEMPLE_NEAR_COMPASS_POT_1, RAND_INF_WATER_TEMPLE_NEAR_COMPASS_POT_1 },
    { RC_WATER_TEMPLE_NEAR_COMPASS_POT_2, RAND_INF_WATER_TEMPLE_NEAR_COMPASS_POT_2 },
    { RC_WATER_TEMPLE_NEAR_COMPASS_POT_3, RAND_INF_WATER_TEMPLE_NEAR_COMPASS_POT_3 },
    { RC_WATER_TEMPLE_CENTRAL_BOW_POT_1, RAND_INF_WATER_TEMPLE_CENTRAL_BOW_POT_1 },
    { RC_WATER_TEMPLE_CENTRAL_BOW_POT_2, RAND_INF_WATER_TEMPLE_CENTRAL_BOW_POT_2 },
    { RC_WATER_TEMPLE_BEHIND_GATE_POT_1, RAND_INF_WATER_TEMPLE_BEHIND_GATE_POT_1 },
    { RC_WATER_TEMPLE_BEHIND_GATE_POT_2, RAND_INF_WATER_TEMPLE_BEHIND_GATE_POT_2 },
    { RC_WATER_TEMPLE_BEHIND_GATE_POT_3, RAND_INF_WATER_TEMPLE_BEHIND_GATE_POT_3 },
    { RC_WATER_TEMPLE_BEHIND_GATE_POT_4, RAND_INF_WATER_TEMPLE_BEHIND_GATE_POT_4 },
    { RC_WATER_TEMPLE_BASEMENT_BLOCK_PUZZLE_POT_1, RAND_INF_WATER_TEMPLE_BASEMENT_BLOCK_PUZZLE_POT_1 },
    { RC_WATER_TEMPLE_BASEMENT_BLOCK_PUZZLE_POT_2, RAND_INF_WATER_TEMPLE_BASEMENT_BLOCK_PUZZLE_POT_2 },
    { RC_WATER_TEMPLE_RIVER_POT_1, RAND_INF_WATER_TEMPLE_RIVER_POT_1 },
    { RC_WATER_TEMPLE_RIVER_POT_2, RAND_INF_WATER_TEMPLE_RIVER_POT_2 },
    { RC_WATER_TEMPLE_LIKE_LIKE_POT_1, RAND_INF_WATER_TEMPLE_LIKE_LIKE_POT_1 },
    { RC_WATER_TEMPLE_LIKE_LIKE_POT_2, RAND_INF_WATER_TEMPLE_LIKE_LIKE_POT_2 },
    { RC_WATER_TEMPLE_BOSS_KEY_POT_1, RAND_INF_WATER_TEMPLE_BOSS_KEY_POT_1 },
    { RC_WATER_TEMPLE_BOSS_KEY_POT_2, RAND_INF_WATER_TEMPLE_BOSS_KEY_POT_2 },
    { RC_SHADOW_TEMPLE_NEAR_DEAD_HAND_POT_1, RAND_INF_SHADOW_TEMPLE_NEAR_DEAD_HAND_POT_1 },
    { RC_SHADOW_TEMPLE_WHISPERING_WALLS_POT_1, RAND_INF_SHADOW_TEMPLE_WHISPERING_WALLS_POT_1 },
    { RC_SHADOW_TEMPLE_WHISPERING_WALLS_POT_2, RAND_INF_SHADOW_TEMPLE_WHISPERING_WALLS_POT_2 },
    { RC_SHADOW_TEMPLE_WHISPERING_WALLS_POT_3, RAND_INF_SHADOW_TEMPLE_WHISPERING_WALLS_POT_3 },
    { RC_SHADOW_TEMPLE_WHISPERING_WALLS_POT_4, RAND_INF_SHADOW_TEMPLE_WHISPERING_WALLS_POT_4 },
    { RC_SHADOW_TEMPLE_WHISPERING_WALLS_POT_5, RAND_INF_SHADOW_TEMPLE_WHISPERING_WALLS_POT_5 },
    { RC_SHADOW_TEMPLE_MAP_CHEST_POT_1, RAND_INF_SHADOW_TEMPLE_MAP_CHEST_POT_1 },
    { RC_SHADOW_TEMPLE_MAP_CHEST_POT_2, RAND_INF_SHADOW_TEMPLE_MAP_CHEST_POT_2 },
    { RC_SHADOW_TEMPLE_FALLING_SPIKES_POT_1, RAND_INF_SHADOW_TEMPLE_FALLING_SPIKES_POT_1 },
    { RC_SHADOW_TEMPLE_FALLING_SPIKES_POT_2, RAND_INF_SHADOW_TEMPLE_FALLING_SPIKES_POT_2 },
    { RC_SHADOW_TEMPLE_FALLING_SPIKES_POT_3, RAND_INF_SHADOW_TEMPLE_FALLING_SPIKES_POT_3 },
    { RC_SHADOW_TEMPLE_FALLING_SPIKES_POT_4, RAND_INF_SHADOW_TEMPLE_FALLING_SPIKES_POT_4 },
    { RC_SHADOW_TEMPLE_AFTER_WIND_POT_1, RAND_INF_SHADOW_TEMPLE_AFTER_WIND_POT_1 },
    { RC_SHADOW_TEMPLE_AFTER_WIND_POT_2, RAND_INF_SHADOW_TEMPLE_AFTER_WIND_POT_2 },
    { RC_SHADOW_TEMPLE_SPIKE_WALLS_POT_1, RAND_INF_SHADOW_TEMPLE_SPIKE_WALLS_POT_1 },
    { RC_SHADOW_TEMPLE_FLOORMASTER_POT_1, RAND_INF_SHADOW_TEMPLE_FLOORMASTER_POT_1 },
    { RC_SHADOW_TEMPLE_FLOORMASTER_POT_2, RAND_INF_SHADOW_TEMPLE_FLOORMASTER_POT_2 },
    { RC_SHADOW_TEMPLE_AFTER_BOAT_POT_1, RAND_INF_SHADOW_TEMPLE_AFTER_BOAT_POT_1 },
    { RC_SHADOW_TEMPLE_AFTER_BOAT_POT_2, RAND_INF_SHADOW_TEMPLE_AFTER_BOAT_POT_2 },
    { RC_SHADOW_TEMPLE_AFTER_BOAT_POT_3, RAND_INF_SHADOW_TEMPLE_AFTER_BOAT_POT_3 },
    { RC_SHADOW_TEMPLE_AFTER_BOAT_POT_4, RAND_INF_SHADOW_TEMPLE_AFTER_BOAT_POT_4 },
    { RC_SPIRIT_TEMPLE_LOBBY_POT_1, RAND_INF_SPIRIT_TEMPLE_LOBBY_POT_1 },
    { RC_SPIRIT_TEMPLE_LOBBY_POT_2, RAND_INF_SPIRIT_TEMPLE_LOBBY_POT_2 },
    { RC_SPIRIT_TEMPLE_ANUBIS_POT_1, RAND_INF_SPIRIT_TEMPLE_ANUBIS_POT_1 },
    { RC_SPIRIT_TEMPLE_ANUBIS_POT_2, RAND_INF_SPIRIT_TEMPLE_ANUBIS_POT_2 },
    { RC_SPIRIT_TEMPLE_ANUBIS_POT_3, RAND_INF_SPIRIT_TEMPLE_ANUBIS_POT_3 },
    { RC_SPIRIT_TEMPLE_ANUBIS_POT_4, RAND_INF_SPIRIT_TEMPLE_ANUBIS_POT_4 },
    { RC_SPIRIT_TEMPLE_CHILD_CLIMB_POT_1, RAND_INF_SPIRIT_TEMPLE_CHILD_CLIMB_POT_1 },
    { RC_SPIRIT_TEMPLE_AFTER_SUN_BLOCK_POT_1, RAND_INF_SPIRIT_TEMPLE_AFTER_SUN_BLOCK_POT_1 },
    { RC_SPIRIT_TEMPLE_AFTER_SUN_BLOCK_POT_2, RAND_INF_SPIRIT_TEMPLE_AFTER_SUN_BLOCK_POT_2 },
    { RC_SPIRIT_TEMPLE_CENTRAL_CHAMBER_POT_1, RAND_INF_SPIRIT_TEMPLE_CENTRAL_CHAMBER_POT_1 },
    { RC_SPIRIT_TEMPLE_CENTRAL_CHAMBER_POT_2, RAND_INF_SPIRIT_TEMPLE_CENTRAL_CHAMBER_POT_2 },
    { RC_SPIRIT_TEMPLE_CENTRAL_CHAMBER_POT_3, RAND_INF_SPIRIT_TEMPLE_CENTRAL_CHAMBER_POT_3 },
    { RC_SPIRIT_TEMPLE_CENTRAL_CHAMBER_POT_4, RAND_INF_SPIRIT_TEMPLE_CENTRAL_CHAMBER_POT_4 },
    { RC_SPIRIT_TEMPLE_CENTRAL_CHAMBER_POT_5, RAND_INF_SPIRIT_TEMPLE_CENTRAL_CHAMBER_POT_5 },
    { RC_SPIRIT_TEMPLE_CENTRAL_CHAMBER_POT_6, RAND_INF_SPIRIT_TEMPLE_CENTRAL_CHAMBER_POT_6 },
    { RC_SPIRIT_TEMPLE_BEAMOS_HALL_POT_1, RAND_INF_SPIRIT_TEMPLE_BEAMOS_HALL_POT_1 },
    { RC_GANONS_CASTLE_FOREST_TRIAL_POT_1, RAND_INF_GANONS_CASTLE_FOREST_TRIAL_POT_1 },
    { RC_GANONS_CASTLE_FOREST_TRIAL_POT_2, RAND_INF_GANONS_CASTLE_FOREST_TRIAL_POT_2 },
    { RC_GANONS_CASTLE_FIRE_TRIAL_POT_1, RAND_INF_GANONS_CASTLE_FIRE_TRIAL_POT_1 },
    { RC_GANONS_CASTLE_FIRE_TRIAL_POT_2, RAND_INF_GANONS_CASTLE_FIRE_TRIAL_POT_2 },
    { RC_GANONS_CASTLE_WATER_TRIAL_POT_1, RAND_INF_GANONS_CASTLE_WATER_TRIAL_POT_1 },
    { RC_GANONS_CASTLE_WATER_TRIAL_POT_2, RAND_INF_GANONS_CASTLE_WATER_TRIAL_POT_2 },
    { RC_GANONS_CASTLE_WATER_TRIAL_POT_3, RAND_INF_GANONS_CASTLE_WATER_TRIAL_POT_3 },
    { RC_GANONS_CASTLE_SHADOW_TRIAL_POT_1, RAND_INF_GANONS_CASTLE_SHADOW_TRIAL_POT_1 },
    { RC_GANONS_CASTLE_SHADOW_TRIAL_POT_2, RAND_INF_GANONS_CASTLE_SHADOW_TRIAL_POT_2 },
    { RC_GANONS_CASTLE_SHADOW_TRIAL_POT_3, RAND_INF_GANONS_CASTLE_SHADOW_TRIAL_POT_3 },
    { RC_GANONS_CASTLE_SHADOW_TRIAL_POT_4, RAND_INF_GANONS_CASTLE_SHADOW_TRIAL_POT_4 },
    { RC_GANONS_CASTLE_SPIRIT_TRIAL_POT_1, RAND_INF_GANONS_CASTLE_SPIRIT_TRIAL_POT_1 },
    { RC_GANONS_CASTLE_SPIRIT_TRIAL_POT_2, RAND_INF_GANONS_CASTLE_SPIRIT_TRIAL_POT_2 },
    { RC_GANONS_CASTLE_LIGHT_TRIAL_BOULDER_POT_1, RAND_INF_GANONS_CASTLE_LIGHT_TRIAL_BOULDER_POT_1 },
    { RC_GANONS_CASTLE_LIGHT_TRIAL_POT_1, RAND_INF_GANONS_CASTLE_LIGHT_TRIAL_POT_1 },
    { RC_GANONS_CASTLE_LIGHT_TRIAL_POT_2, RAND_INF_GANONS_CASTLE_LIGHT_TRIAL_POT_2 },
    { RC_GANONS_CASTLE_GANONS_TOWER_POT_1, RAND_INF_GANONS_CASTLE_GANONS_TOWER_POT_1 },
    { RC_GANONS_CASTLE_GANONS_TOWER_POT_2, RAND_INF_GANONS_CASTLE_GANONS_TOWER_POT_2 },
    { RC_GANONS_CASTLE_GANONS_TOWER_POT_3, RAND_INF_GANONS_CASTLE_GANONS_TOWER_POT_3 },
    { RC_GANONS_CASTLE_GANONS_TOWER_POT_4, RAND_INF_GANONS_CASTLE_GANONS_TOWER_POT_4 },
    { RC_GANONS_CASTLE_GANONS_TOWER_POT_5, RAND_INF_GANONS_CASTLE_GANONS_TOWER_POT_5 },
    { RC_GANONS_CASTLE_GANONS_TOWER_POT_6, RAND_INF_GANONS_CASTLE_GANONS_TOWER_POT_6 },
    { RC_GANONS_CASTLE_GANONS_TOWER_POT_7, RAND_INF_GANONS_CASTLE_GANONS_TOWER_POT_7 },
    { RC_GANONS_CASTLE_GANONS_TOWER_POT_8, RAND_INF_GANONS_CASTLE_GANONS_TOWER_POT_8 },
    { RC_GANONS_CASTLE_GANONS_TOWER_POT_9, RAND_INF_GANONS_CASTLE_GANONS_TOWER_POT_9 },
    { RC_GANONS_CASTLE_GANONS_TOWER_POT_10, RAND_INF_GANONS_CASTLE_GANONS_TOWER_POT_10 },
    { RC_GANONS_CASTLE_GANONS_TOWER_POT_11, RAND_INF_GANONS_CASTLE_GANONS_TOWER_POT_11 },
    { RC_GANONS_CASTLE_GANONS_TOWER_POT_12, RAND_INF_GANONS_CASTLE_GANONS_TOWER_POT_12 },
    { RC_GANONS_CASTLE_GANONS_TOWER_POT_13, RAND_INF_GANONS_CASTLE_GANONS_TOWER_POT_13 },
    { RC_GANONS_CASTLE_GANONS_TOWER_POT_14, RAND_INF_GANONS_CASTLE_GANONS_TOWER_POT_14 },
    { RC_GANONS_CASTLE_GANONS_TOWER_POT_15, RAND_INF_GANONS_CASTLE_GANONS_TOWER_POT_15 },
    { RC_GANONS_CASTLE_GANONS_TOWER_POT_16, RAND_INF_GANONS_CASTLE_GANONS_TOWER_POT_16 },
    { RC_GANONS_CASTLE_GANONS_TOWER_POT_17, RAND_INF_GANONS_CASTLE_GANONS_TOWER_POT_17 },
    { RC_GANONS_CASTLE_GANONS_TOWER_POT_18, RAND_INF_GANONS_CASTLE_GANONS_TOWER_POT_18 },
    { RC_BOTTOM_OF_THE_WELL_BASEMENT_POT_1, RAND_INF_BOTTOM_OF_THE_WELL_BASEMENT_POT_1 },
    { RC_BOTTOM_OF_THE_WELL_BASEMENT_POT_2, RAND_INF_BOTTOM_OF_THE_WELL_BASEMENT_POT_2 },
    { RC_BOTTOM_OF_THE_WELL_BASEMENT_POT_3, RAND_INF_BOTTOM_OF_THE_WELL_BASEMENT_POT_3 },
    { RC_BOTTOM_OF_THE_WELL_BASEMENT_POT_4, RAND_INF_BOTTOM_OF_THE_WELL_BASEMENT_POT_4 },
    { RC_BOTTOM_OF_THE_WELL_BASEMENT_POT_5, RAND_INF_BOTTOM_OF_THE_WELL_BASEMENT_POT_5 },
    { RC_BOTTOM_OF_THE_WELL_BASEMENT_POT_6, RAND_INF_BOTTOM_OF_THE_WELL_BASEMENT_POT_6 },
    { RC_BOTTOM_OF_THE_WELL_BASEMENT_POT_7, RAND_INF_BOTTOM_OF_THE_WELL_BASEMENT_POT_7 },
    { RC_BOTTOM_OF_THE_WELL_BASEMENT_POT_8, RAND_INF_BOTTOM_OF_THE_WELL_BASEMENT_POT_8 },
    { RC_BOTTOM_OF_THE_WELL_BASEMENT_POT_9, RAND_INF_BOTTOM_OF_THE_WELL_BASEMENT_POT_9 },
    { RC_BOTTOM_OF_THE_WELL_BASEMENT_POT_10, RAND_INF_BOTTOM_OF_THE_WELL_BASEMENT_POT_10 },
    { RC_BOTTOM_OF_THE_WELL_BASEMENT_POT_11, RAND_INF_BOTTOM_OF_THE_WELL_BASEMENT_POT_11 },
    { RC_BOTTOM_OF_THE_WELL_BASEMENT_POT_12, RAND_INF_BOTTOM_OF_THE_WELL_BASEMENT_POT_12 },
    { RC_BOTTOM_OF_THE_WELL_LEFT_SIDE_POT_1, RAND_INF_BOTTOM_OF_THE_WELL_LEFT_SIDE_POT_1 },
    { RC_BOTTOM_OF_THE_WELL_LEFT_SIDE_POT_2, RAND_INF_BOTTOM_OF_THE_WELL_LEFT_SIDE_POT_2 },
    { RC_BOTTOM_OF_THE_WELL_LEFT_SIDE_POT_3, RAND_INF_BOTTOM_OF_THE_WELL_LEFT_SIDE_POT_3 },
    { RC_BOTTOM_OF_THE_WELL_NEAR_ENTRANCE_POT_1, RAND_INF_BOTTOM_OF_THE_WELL_NEAR_ENTRANCE_POT_1 },
    { RC_BOTTOM_OF_THE_WELL_NEAR_ENTRANCE_POT_2, RAND_INF_BOTTOM_OF_THE_WELL_NEAR_ENTRANCE_POT_2 },
    { RC_BOTTOM_OF_THE_WELL_FIRE_KEESE_POT_1, RAND_INF_BOTTOM_OF_THE_WELL_FIRE_KEESE_POT_1 },
    { RC_BOTTOM_OF_THE_WELL_UNDERWATER_POT, RAND_INF_BOTTOM_OF_THE_WELL_UNDERWATER_POT },
    { RC_ICE_CAVERN_HALL_POT_1, RAND_INF_ICE_CAVERN_HALL_POT_1 },
    { RC_ICE_CAVERN_HALL_POT_2, RAND_INF_ICE_CAVERN_HALL_POT_2 },
    { RC_ICE_CAVERN_SPINNING_BLADE_POT_1, RAND_INF_ICE_CAVERN_SPINNING_BLADE_POT_1 },
    { RC_ICE_CAVERN_SPINNING_BLADE_POT_2, RAND_INF_ICE_CAVERN_SPINNING_BLADE_POT_2 },
    { RC_ICE_CAVERN_SPINNING_BLADE_POT_3, RAND_INF_ICE_CAVERN_SPINNING_BLADE_POT_3 },
    { RC_ICE_CAVERN_NEAR_END_POT_1, RAND_INF_ICE_CAVERN_NEAR_END_POT_1 },
    { RC_ICE_CAVERN_NEAR_END_POT_2, RAND_INF_ICE_CAVERN_NEAR_END_POT_2 },
    { RC_ICE_CAVERN_FROZEN_POT_1, RAND_INF_ICE_CAVERN_FROZEN_POT_1 },

    { RC_JABU_JABUS_BELLY_MQ_ENTRANCE_POT_1, RAND_INF_JABU_JABUS_BELLY_MQ_ENTRANCE_POT_1 },
    { RC_JABU_JABUS_BELLY_MQ_ENTRANCE_POT_2, RAND_INF_JABU_JABUS_BELLY_MQ_ENTRANCE_POT_2 },
    { RC_JABU_JABUS_BELLY_MQ_GEYSER_POT_1, RAND_INF_JABU_JABUS_BELLY_MQ_GEYSER_POT_1 },
    { RC_JABU_JABUS_BELLY_MQ_GEYSER_POT_2, RAND_INF_JABU_JABUS_BELLY_MQ_GEYSER_POT_2 },
    { RC_JABU_JABUS_BELLY_MQ_TIME_BLOCK_POT_1, RAND_INF_JABU_JABUS_BELLY_MQ_TIME_BLOCK_POT_1 },
    { RC_JABU_JABUS_BELLY_MQ_TIME_BLOCK_POT_2, RAND_INF_JABU_JABUS_BELLY_MQ_TIME_BLOCK_POT_2 },
    { RC_JABU_JABUS_BELLY_MQ_LIKE_LIKES_POT_1, RAND_INF_JABU_JABUS_BELLY_MQ_LIKE_LIKES_POT_1 },
    { RC_JABU_JABUS_BELLY_MQ_LIKE_LIKES_POT_2, RAND_INF_JABU_JABUS_BELLY_MQ_LIKE_LIKES_POT_2 },
    { RC_JABU_JABUS_BELLY_MQ_BEFORE_BOSS_POT_1, RAND_INF_JABU_JABUS_BELLY_MQ_BEFORE_BOSS_POT_1 },
    { RC_FOREST_TEMPLE_MQ_LOBBY_POT_1, RAND_INF_FOREST_TEMPLE_MQ_LOBBY_POT_1 },
    { RC_FOREST_TEMPLE_MQ_LOBBY_POT_2, RAND_INF_FOREST_TEMPLE_MQ_LOBBY_POT_2 },
    { RC_FOREST_TEMPLE_MQ_LOBBY_POT_3, RAND_INF_FOREST_TEMPLE_MQ_LOBBY_POT_3 },
    { RC_FOREST_TEMPLE_MQ_LOBBY_POT_4, RAND_INF_FOREST_TEMPLE_MQ_LOBBY_POT_4 },
    { RC_FOREST_TEMPLE_MQ_LOBBY_POT_5, RAND_INF_FOREST_TEMPLE_MQ_LOBBY_POT_5 },
    { RC_FOREST_TEMPLE_MQ_LOBBY_POT_6, RAND_INF_FOREST_TEMPLE_MQ_LOBBY_POT_6 },
    { RC_FOREST_TEMPLE_MQ_WOLFOS_POT_1, RAND_INF_FOREST_TEMPLE_MQ_LOWER_STALFOS_POT_1 },
    { RC_FOREST_TEMPLE_MQ_WOLFOS_POT_2, RAND_INF_FOREST_TEMPLE_MQ_LOWER_STALFOS_POT_2 },
    { RC_FOREST_TEMPLE_MQ_UPPER_STALFOS_POT_1, RAND_INF_FOREST_TEMPLE_MQ_UPPER_STALFOS_POT_1 },
    { RC_FOREST_TEMPLE_MQ_UPPER_STALFOS_POT_2, RAND_INF_FOREST_TEMPLE_MQ_UPPER_STALFOS_POT_2 },
    { RC_FOREST_TEMPLE_MQ_UPPER_STALFOS_POT_3, RAND_INF_FOREST_TEMPLE_MQ_UPPER_STALFOS_POT_3 },
    { RC_FOREST_TEMPLE_MQ_UPPER_STALFOS_POT_4, RAND_INF_FOREST_TEMPLE_MQ_UPPER_STALFOS_POT_4 },
    { RC_FOREST_TEMPLE_MQ_BLUE_POE_POT_1, RAND_INF_FOREST_TEMPLE_MQ_BLUE_POE_POT_1 },
    { RC_FOREST_TEMPLE_MQ_BLUE_POE_POT_2, RAND_INF_FOREST_TEMPLE_MQ_BLUE_POE_POT_2 },
    { RC_FOREST_TEMPLE_MQ_BLUE_POE_POT_3, RAND_INF_FOREST_TEMPLE_MQ_BLUE_POE_POT_3 },
    { RC_FOREST_TEMPLE_MQ_GREEN_POE_POT_1, RAND_INF_FOREST_TEMPLE_MQ_GREEN_POE_POT_1 },
    { RC_FOREST_TEMPLE_MQ_GREEN_POE_POT_2, RAND_INF_FOREST_TEMPLE_MQ_GREEN_POE_POT_2 },
    { RC_FOREST_TEMPLE_MQ_BASEMENT_POT_1, RAND_INF_FOREST_TEMPLE_MQ_BASEMENT_POT_1 },
    { RC_FOREST_TEMPLE_MQ_BASEMENT_POT_2, RAND_INF_FOREST_TEMPLE_MQ_BASEMENT_POT_2 },
    { RC_FOREST_TEMPLE_MQ_BASEMENT_POT_3, RAND_INF_FOREST_TEMPLE_MQ_BASEMENT_POT_3 },
    { RC_FOREST_TEMPLE_MQ_BASEMENT_POT_4, RAND_INF_FOREST_TEMPLE_MQ_BASEMENT_POT_4 },
    { RC_DODONGOS_CAVERN_MQ_RIGHT_SIDE_POT_1, RAND_INF_DODONGOS_CAVERN_MQ_RIGHT_SIDE_POT_1 },
    { RC_DODONGOS_CAVERN_MQ_RIGHT_SIDE_POT_2, RAND_INF_DODONGOS_CAVERN_MQ_RIGHT_SIDE_POT_2 },
    { RC_DODONGOS_CAVERN_MQ_RIGHT_SIDE_POT_3, RAND_INF_DODONGOS_CAVERN_MQ_RIGHT_SIDE_POT_3 },
    { RC_DODONGOS_CAVERN_MQ_RIGHT_SIDE_POT_4, RAND_INF_DODONGOS_CAVERN_MQ_RIGHT_SIDE_POT_4 },
    { RC_DODONGOS_CAVERN_MQ_UPPER_LIZALFOS_POT_1, RAND_INF_DODONGOS_CAVERN_MQ_UPPER_LIZALFOS_POT_1 },
    { RC_DODONGOS_CAVERN_MQ_UPPER_LIZALFOS_POT_2, RAND_INF_DODONGOS_CAVERN_MQ_UPPER_LIZALFOS_POT_2 },
    { RC_DODONGOS_CAVERN_MQ_UPPER_LIZALFOS_POT_3, RAND_INF_DODONGOS_CAVERN_MQ_UPPER_LIZALFOS_POT_3 },
    { RC_DODONGOS_CAVERN_MQ_UPPER_LIZALFOS_POT_4, RAND_INF_DODONGOS_CAVERN_MQ_UPPER_LIZALFOS_POT_4 },
    { RC_DODONGOS_CAVERN_MQ_POE_ROOM_POT_1, RAND_INF_DODONGOS_CAVERN_MQ_POE_ROOM_POT_1 },
    { RC_DODONGOS_CAVERN_MQ_POE_ROOM_POT_2, RAND_INF_DODONGOS_CAVERN_MQ_POE_ROOM_POT_2 },
    { RC_DODONGOS_CAVERN_MQ_POE_ROOM_POT_3, RAND_INF_DODONGOS_CAVERN_MQ_POE_ROOM_POT_3 },
    { RC_DODONGOS_CAVERN_MQ_POE_ROOM_POT_4, RAND_INF_DODONGOS_CAVERN_MQ_POE_ROOM_POT_4 },
    { RC_DODONGOS_CAVERN_MQ_TORCH_PUZZLE_CORNER_POT, RAND_INF_DODONGOS_CAVERN_MQ_BLOCK_ROOM_POT_1 },
    { RC_DODONGOS_CAVERN_MQ_TORCH_PUZZLE_MIDDLE_POT, RAND_INF_DODONGOS_CAVERN_MQ_BLOCK_ROOM_POT_2 },
    { RC_DODONGOS_CAVERN_MQ_TWO_FLAMES_POT_1, RAND_INF_DODONGOS_CAVERN_MQ_TWO_FLAMES_POT_1 },
    { RC_DODONGOS_CAVERN_MQ_TWO_FLAMES_POT_2, RAND_INF_DODONGOS_CAVERN_MQ_TWO_FLAMES_POT_2 },
    { RC_DODONGOS_CAVERN_MQ_BIG_BLOCK_POT_1, RAND_INF_DODONGOS_CAVERN_MQ_SILVER_BLOCK_POT_1 },
    { RC_DODONGOS_CAVERN_MQ_BIG_BLOCK_POT_2, RAND_INF_DODONGOS_CAVERN_MQ_SILVER_BLOCK_POT_2 },
    { RC_DODONGOS_CAVERN_MQ_STAIRCASE_POT_1, RAND_INF_DODONGOS_CAVERN_MQ_STAIRCASE_POT_1 },
    { RC_DODONGOS_CAVERN_MQ_STAIRCASE_POT_2, RAND_INF_DODONGOS_CAVERN_MQ_STAIRCASE_POT_2 },
    { RC_DODONGOS_CAVERN_MQ_STAIRCASE_POT_3, RAND_INF_DODONGOS_CAVERN_MQ_STAIRCASE_POT_3 },
    { RC_DODONGOS_CAVERN_MQ_STAIRCASE_POT_4, RAND_INF_DODONGOS_CAVERN_MQ_STAIRCASE_POT_4 },
    { RC_DODONGOS_CAVERN_MQ_ARMOS_ROOM_NW_POT, RAND_INF_DODONGOS_CAVERN_MQ_ARMOS_POT_1 },
    { RC_DODONGOS_CAVERN_MQ_ARMOS_ROOM_NE_POT, RAND_INF_DODONGOS_CAVERN_MQ_ARMOS_POT_2 },
    { RC_DODONGOS_CAVERN_MQ_ARMOS_ROOM_SE_POT, RAND_INF_DODONGOS_CAVERN_MQ_ARMOS_POT_3 },
    { RC_DODONGOS_CAVERN_MQ_ARMOS_ROOM_SW_POT, RAND_INF_DODONGOS_CAVERN_MQ_ARMOS_POT_4 },
    { RC_DODONGOS_CAVERN_MQ_BEFORE_BOSS_SW_POT, RAND_INF_DODONGOS_CAVERN_MQ_BEFORE_BOSS_POT_1 },
    { RC_DODONGOS_CAVERN_MQ_BEFORE_BOSS_NE_POT, RAND_INF_DODONGOS_CAVERN_MQ_BEFORE_BOSS_POT_2 },
    { RC_DODONGOS_CAVERN_MQ_BACKROOM_POT_1, RAND_INF_DODONGOS_CAVERN_MQ_BACKROOM_POT_1 },
    { RC_DODONGOS_CAVERN_MQ_BACKROOM_POT_2, RAND_INF_DODONGOS_CAVERN_MQ_BACKROOM_POT_2 },
    { RC_GANONS_CASTLE_MQ_FOREST_TRIAL_POT_1, RAND_INF_GANONS_CASTLE_MQ_FOREST_TRIAL_POT_1 },
    { RC_GANONS_CASTLE_MQ_FOREST_TRIAL_POT_2, RAND_INF_GANONS_CASTLE_MQ_FOREST_TRIAL_POT_2 },
    { RC_GANONS_CASTLE_MQ_WATER_TRIAL_POT_1, RAND_INF_GANONS_CASTLE_MQ_WATER_TRIAL_POT_1 },
    { RC_GANONS_CASTLE_MQ_WATER_TRIAL_POT_2, RAND_INF_GANONS_CASTLE_MQ_WATER_TRIAL_POT_2 },
    { RC_GANONS_CASTLE_MQ_SHADOW_TRIAL_POT_1, RAND_INF_GANONS_CASTLE_MQ_SHADOW_TRIAL_POT_1 },
    { RC_GANONS_CASTLE_MQ_SHADOW_TRIAL_POT_2, RAND_INF_GANONS_CASTLE_MQ_SHADOW_TRIAL_POT_2 },
    { RC_GANONS_CASTLE_MQ_FIRE_TRIAL_POT_1, RAND_INF_GANONS_CASTLE_MQ_FIRE_TRIAL_POT_1 },
    { RC_GANONS_CASTLE_MQ_FIRE_TRIAL_POT_2, RAND_INF_GANONS_CASTLE_MQ_FIRE_TRIAL_POT_2 },
    { RC_GANONS_CASTLE_MQ_LIGHT_TRIAL_POT_1, RAND_INF_GANONS_CASTLE_MQ_LIGHT_TRIAL_POT_1 },
    { RC_GANONS_CASTLE_MQ_LIGHT_TRIAL_POT_2, RAND_INF_GANONS_CASTLE_MQ_LIGHT_TRIAL_POT_2 },
    { RC_GANONS_CASTLE_MQ_SPIRIT_TRIAL_POT_1, RAND_INF_GANONS_CASTLE_MQ_SPIRIT_TRIAL_POT_1 },
    { RC_GANONS_CASTLE_MQ_SPIRIT_TRIAL_POT_2, RAND_INF_GANONS_CASTLE_MQ_SPIRIT_TRIAL_POT_2 },
    { RC_SHADOW_TEMPLE_MQ_WHISPERING_WALLS_POT_1, RAND_INF_SHADOW_TEMPLE_MQ_WHISPERING_WALLS_POT_1 },
    { RC_SHADOW_TEMPLE_MQ_WHISPERING_WALLS_POT_2, RAND_INF_SHADOW_TEMPLE_MQ_WHISPERING_WALLS_POT_2 },
    { RC_SHADOW_TEMPLE_MQ_ENTRANCE_REDEAD_POT_1, RAND_INF_SHADOW_TEMPLE_MQ_ENTRANCE_REDEAD_POT_1 },
    { RC_SHADOW_TEMPLE_MQ_ENTRANCE_REDEAD_POT_2, RAND_INF_SHADOW_TEMPLE_MQ_ENTRANCE_REDEAD_POT_2 },
    { RC_SHADOW_TEMPLE_MQ_LOWER_UMBRELLA_WEST_POT, RAND_INF_SHADOW_TEMPLE_MQ_FALLING_SPIKES_POT_1 },
    { RC_SHADOW_TEMPLE_MQ_LOWER_UMBRELLA_EAST_POT, RAND_INF_SHADOW_TEMPLE_MQ_FALLING_SPIKES_POT_2 },
    { RC_SHADOW_TEMPLE_MQ_UPPER_UMBRELLA_SOUTH_POT, RAND_INF_SHADOW_TEMPLE_MQ_FALLING_SPIKES_POT_3 },
    { RC_SHADOW_TEMPLE_MQ_UPPER_UMBRELLA_NORTH_POT, RAND_INF_SHADOW_TEMPLE_MQ_FALLING_SPIKES_POT_4 },
    { RC_SHADOW_TEMPLE_MQ_BEFORE_BOAT_POT_1, RAND_INF_SHADOW_TEMPLE_MQ_BEFORE_BOAT_POT_1 },
    { RC_SHADOW_TEMPLE_MQ_BEFORE_BOAT_POT_2, RAND_INF_SHADOW_TEMPLE_MQ_BEFORE_BOAT_POT_2 },
    { RC_SHADOW_TEMPLE_MQ_BEFORE_CHASM_WEST_POT, RAND_INF_SHADOW_TEMPLE_MQ_AFTER_BOAT_POT_1 },
    { RC_SHADOW_TEMPLE_MQ_BEFORE_CHASM_EAST_POT, RAND_INF_SHADOW_TEMPLE_MQ_AFTER_BOAT_POT_2 },
    { RC_SHADOW_TEMPLE_MQ_AFTER_CHASM_WEST_POT, RAND_INF_SHADOW_TEMPLE_MQ_AFTER_BOAT_POT_3 },
    { RC_SHADOW_TEMPLE_MQ_AFTER_CHASM_EAST_POT, RAND_INF_SHADOW_TEMPLE_MQ_AFTER_BOAT_POT_4 },
    { RC_SHADOW_TEMPLE_MQ_SPIKE_BARICADE_POT, RAND_INF_SHADOW_TEMPLE_MQ_SPIKE_BARICADE_POT },
    { RC_SHADOW_TEMPLE_MQ_DEAD_HAND_POT_1, RAND_INF_SHADOW_TEMPLE_MQ_DEAD_HAND_POT_1 },
    { RC_SHADOW_TEMPLE_MQ_DEAD_HAND_POT_2, RAND_INF_SHADOW_TEMPLE_MQ_DEAD_HAND_POT_2 },
    { RC_BOTTOM_OF_THE_WELL_MQ_INNER_LOBBY_POT_1, RAND_INF_BOTTOM_OF_THE_WELL_MQ_INNER_LOBBY_POT_1 },
    { RC_BOTTOM_OF_THE_WELL_MQ_INNER_LOBBY_POT_2, RAND_INF_BOTTOM_OF_THE_WELL_MQ_INNER_LOBBY_POT_2 },
    { RC_BOTTOM_OF_THE_WELL_MQ_INNER_LOBBY_POT_3, RAND_INF_BOTTOM_OF_THE_WELL_MQ_INNER_LOBBY_POT_3 },
    { RC_BOTTOM_OF_THE_WELL_MQ_OUTER_LOBBY_POT, RAND_INF_BOTTOM_OF_THE_WELL_MQ_OUTER_LOBBY_POT },
    { RC_BOTTOM_OF_THE_WELL_MQ_EAST_INNER_ROOM_POT_1, RAND_INF_BOTTOM_OF_THE_WELL_MQ_SOUTH_KEY_POT_1 },
    { RC_BOTTOM_OF_THE_WELL_MQ_EAST_INNER_ROOM_POT_2, RAND_INF_BOTTOM_OF_THE_WELL_MQ_SOUTH_KEY_POT_2 },
    { RC_BOTTOM_OF_THE_WELL_MQ_EAST_INNER_ROOM_POT_3, RAND_INF_BOTTOM_OF_THE_WELL_MQ_SOUTH_KEY_POT_3 },
    { RC_FIRE_TEMPLE_MQ_ENTRANCE_POT_1, RAND_INF_FIRE_TEMPLE_MQ_ENTRANCE_POT_1 },
    { RC_FIRE_TEMPLE_MQ_ENTRANCE_POT_2, RAND_INF_FIRE_TEMPLE_MQ_ENTRANCE_POT_2 },
    { RC_FIRE_TEMPLE_MQ_OUTSIDE_BOSS_POT_1, RAND_INF_FIRE_TEMPLE_MQ_OUTSIDE_BOSS_POT_1 },
    { RC_FIRE_TEMPLE_MQ_OUTSIDE_BOSS_POT_2, RAND_INF_FIRE_TEMPLE_MQ_OUTSIDE_BOSS_POT_2 },
    { RC_FIRE_TEMPLE_MQ_LAVA_ROOM_NORTH_POT, RAND_INF_FIRE_TEMPLE_MQ_LAVA_POT_1 },
    { RC_FIRE_TEMPLE_MQ_LAVA_ROOM_HIGH_POT, RAND_INF_FIRE_TEMPLE_MQ_LAVA_POT_2 },
    { RC_FIRE_TEMPLE_MQ_LAVA_ROOM_SOUTH_POT, RAND_INF_FIRE_TEMPLE_MQ_LAVA_POT_3 },
    { RC_FIRE_TEMPLE_MQ_LAVA_TORCH_POT_1, RAND_INF_FIRE_TEMPLE_MQ_LAVA_TORCH_POT_1 },
    { RC_FIRE_TEMPLE_MQ_LAVA_TORCH_POT_2, RAND_INF_FIRE_TEMPLE_MQ_LAVA_TORCH_POT_2 },
    { RC_FIRE_TEMPLE_MQ_ABOVE_LAVA_POT_1, RAND_INF_FIRE_TEMPLE_MQ_ABOVE_LAVA_POT_1 },
    { RC_FIRE_TEMPLE_MQ_ABOVE_LAVA_POT_2, RAND_INF_FIRE_TEMPLE_MQ_ABOVE_LAVA_POT_2 },
    { RC_FIRE_TEMPLE_MQ_ABOVE_LAVA_POT_3, RAND_INF_FIRE_TEMPLE_MQ_ABOVE_LAVA_POT_3 },
    { RC_FIRE_TEMPLE_MQ_FLAME_WALL_POT_1, RAND_INF_FIRE_TEMPLE_MQ_FLAME_WALL_POT_1 },
    { RC_FIRE_TEMPLE_MQ_FLAME_WALL_POT_2, RAND_INF_FIRE_TEMPLE_MQ_FLAME_WALL_POT_2 },
    { RC_FIRE_TEMPLE_MQ_PAST_FIRE_MAZE_SOUTH_POT, RAND_INF_FIRE_TEMPLE_MQ_FIRE_MAZE_POT_1 },
    { RC_FIRE_TEMPLE_MQ_PAST_FIRE_MAZE_NORTH_POT, RAND_INF_FIRE_TEMPLE_MQ_FIRE_MAZE_POT_2 },
    { RC_FIRE_TEMPLE_MQ_FIRE_MAZE_NORTHMOST_POT, RAND_INF_FIRE_TEMPLE_MQ_FIRE_MAZE_POT_3 },
    { RC_FIRE_TEMPLE_MQ_FIRE_MAZE_NORTHWEST_POT, RAND_INF_FIRE_TEMPLE_MQ_FIRE_MAZE_POT_4 },
    { RC_FIRE_TEMPLE_MQ_SOUTH_FIRE_MAZE_WEST_POT, RAND_INF_FIRE_TEMPLE_MQ_FIRE_MAZE_POT_5 },
    { RC_FIRE_TEMPLE_MQ_SOUTH_FIRE_MAZE_EAST_POT, RAND_INF_FIRE_TEMPLE_MQ_FIRE_MAZE_POT_6 },
    { RC_FIRE_TEMPLE_MQ_BEFORE_MINI_BOSS_POT_1, RAND_INF_FIRE_TEMPLE_MQ_BEFORE_MINI_BOSS_POT_1 },
    { RC_FIRE_TEMPLE_MQ_BEFORE_MINI_BOSS_POT_2, RAND_INF_FIRE_TEMPLE_MQ_BEFORE_MINI_BOSS_POT_2 },
    { RC_FIRE_TEMPLE_MQ_BEFORE_MINI_BOSS_POT_3, RAND_INF_FIRE_TEMPLE_MQ_BEFORE_MINI_BOSS_POT_3 },
    { RC_FIRE_TEMPLE_MQ_BEFORE_MINI_BOSS_POT_4, RAND_INF_FIRE_TEMPLE_MQ_BEFORE_MINI_BOSS_POT_4 },
    { RC_FIRE_TEMPLE_MQ_BEFORE_MINI_BOSS_POT_5, RAND_INF_FIRE_TEMPLE_MQ_BEFORE_MINI_BOSS_POT_5 },
    { RC_FIRE_TEMPLE_MQ_BEFORE_MINI_BOSS_POT_6, RAND_INF_FIRE_TEMPLE_MQ_BEFORE_MINI_BOSS_POT_6 },
    { RC_FIRE_TEMPLE_MQ_BEFORE_MINI_BOSS_POT_7, RAND_INF_FIRE_TEMPLE_MQ_BEFORE_MINI_BOSS_POT_7 },
    { RC_FIRE_TEMPLE_MQ_BEFORE_MINI_BOSS_POT_8, RAND_INF_FIRE_TEMPLE_MQ_BEFORE_MINI_BOSS_POT_8 },
    { RC_ICE_CAVERN_MQ_ENTRANCE_POT, RAND_INF_ICE_CAVERN_MQ_ENTRANCE_POT },
    { RC_ICE_CAVERN_MQ_FIRST_CRYSTAL_POT_1, RAND_INF_ICE_CAVERN_MQ_FIRST_CRYSTAL_POT_1 },
    { RC_ICE_CAVERN_MQ_FIRST_CRYSTAL_POT_2, RAND_INF_ICE_CAVERN_MQ_FIRST_CRYSTAL_POT_2 },
    { RC_ICE_CAVERN_MQ_EARLY_WOLFOS_POT_1, RAND_INF_ICE_CAVERN_MQ_EARLY_WOLFOS_POT_1 },
    { RC_ICE_CAVERN_MQ_EARLY_WOLFOS_POT_2, RAND_INF_ICE_CAVERN_MQ_EARLY_WOLFOS_POT_2 },
    { RC_ICE_CAVERN_MQ_EARLY_WOLFOS_POT_3, RAND_INF_ICE_CAVERN_MQ_EARLY_WOLFOS_POT_3 },
    { RC_ICE_CAVERN_MQ_EARLY_WOLFOS_POT_4, RAND_INF_ICE_CAVERN_MQ_EARLY_WOLFOS_POT_4 },
    { RC_ICE_CAVERN_MQ_PUSH_BLOCK_POT_1, RAND_INF_ICE_CAVERN_MQ_PUSH_BLOCK_POT_1 },
    { RC_ICE_CAVERN_MQ_PUSH_BLOCK_POT_2, RAND_INF_ICE_CAVERN_MQ_PUSH_BLOCK_POT_2 },
    { RC_ICE_CAVERN_MQ_COMPASS_POT_1, RAND_INF_ICE_CAVERN_MQ_COMPASS_POT_1 },
    { RC_ICE_CAVERN_MQ_COMPASS_POT_2, RAND_INF_ICE_CAVERN_MQ_COMPASS_POT_2 },
    { RC_SPIRIT_TEMPLE_MQ_ENTRANCE_POT_1, RAND_INF_SPIRIT_TEMPLE_MQ_ENTRANCE_POT_1 },
    { RC_SPIRIT_TEMPLE_MQ_ENTRANCE_POT_2, RAND_INF_SPIRIT_TEMPLE_MQ_ENTRANCE_POT_2 },
    { RC_SPIRIT_TEMPLE_MQ_ENTRANCE_POT_3, RAND_INF_SPIRIT_TEMPLE_MQ_ENTRANCE_POT_3 },
    { RC_SPIRIT_TEMPLE_MQ_ENTRANCE_POT_4, RAND_INF_SPIRIT_TEMPLE_MQ_ENTRANCE_POT_4 },
    { RC_SPIRIT_TEMPLE_MQ_CHILD_SLUGMA_POT, RAND_INF_SPIRIT_TEMPLE_MQ_CHILD_SLUGMA_POT },
    { RC_SPIRIT_TEMPLE_MQ_CHILD_GIBDO_POT_1, RAND_INF_SPIRIT_TEMPLE_MQ_CHILD_GIBDO_POT_1 },
    { RC_SPIRIT_TEMPLE_MQ_CHILD_GIBDO_POT_2, RAND_INF_SPIRIT_TEMPLE_MQ_CHILD_GIBDO_POT_2 },
    { RC_SPIRIT_TEMPLE_MQ_CHILD_LIKE_LIKE_POT, RAND_INF_SPIRIT_TEMPLE_MQ_CHILD_LIKE_LIKE_POT },
    { RC_SPIRIT_TEMPLE_MQ_CHILD_STALFOS_POT_1, RAND_INF_SPIRIT_TEMPLE_MQ_CHILD_STALFOS_POT_1 },
    { RC_SPIRIT_TEMPLE_MQ_CHILD_STALFOS_POT_2, RAND_INF_SPIRIT_TEMPLE_MQ_CHILD_STALFOS_POT_2 },
    { RC_SPIRIT_TEMPLE_MQ_CHILD_STALFOS_POT_3, RAND_INF_SPIRIT_TEMPLE_MQ_CHILD_STALFOS_POT_3 },
    { RC_SPIRIT_TEMPLE_MQ_CHILD_STALFOS_POT_4, RAND_INF_SPIRIT_TEMPLE_MQ_CHILD_STALFOS_POT_4 },
    { RC_SPIRIT_TEMPLE_MQ_STATUE_2F_CENTER_EAST_POT, RAND_INF_SPIRIT_TEMPLE_MQ_CENTRAL_CHAMBER_POT_1 },
    { RC_SPIRIT_TEMPLE_MQ_STATUE_3F_EAST_POT, RAND_INF_SPIRIT_TEMPLE_MQ_CENTRAL_CHAMBER_POT_2 },
    { RC_SPIRIT_TEMPLE_MQ_STATUE_3F_WEST_POT, RAND_INF_SPIRIT_TEMPLE_MQ_CENTRAL_CHAMBER_POT_3 },
    { RC_SPIRIT_TEMPLE_MQ_STATUE_2F_WEST_POT, RAND_INF_SPIRIT_TEMPLE_MQ_CENTRAL_CHAMBER_POT_4 },
    { RC_SPIRIT_TEMPLE_MQ_STATUE_2F_EASTMOST_POT, RAND_INF_SPIRIT_TEMPLE_MQ_CENTRAL_CHAMBER_POT_5 },
    { RC_SPIRIT_TEMPLE_MQ_SUN_BLOCKS_POT_1, RAND_INF_SPIRIT_TEMPLE_MQ_SUN_BLOCKS_POT_1 },
    { RC_SPIRIT_TEMPLE_MQ_SUN_BLOCKS_POT_2, RAND_INF_SPIRIT_TEMPLE_MQ_SUN_BLOCKS_POT_2 },
    { RC_SPIRIT_TEMPLE_MQ_LONG_CLIMB_POT_1, RAND_INF_SPIRIT_TEMPLE_MQ_LONG_CLIMB_POT_1 },
    { RC_SPIRIT_TEMPLE_MQ_LONG_CLIMB_POT_2, RAND_INF_SPIRIT_TEMPLE_MQ_LONG_CLIMB_POT_2 },
    { RC_SPIRIT_TEMPLE_MQ_BIG_MIRROR_POT_1, RAND_INF_SPIRIT_TEMPLE_MQ_BIG_MIRROR_POT_1 },
    { RC_SPIRIT_TEMPLE_MQ_BIG_MIRROR_POT_2, RAND_INF_SPIRIT_TEMPLE_MQ_BIG_MIRROR_POT_2 },
    { RC_SPIRIT_TEMPLE_MQ_BIG_MIRROR_POT_3, RAND_INF_SPIRIT_TEMPLE_MQ_BIG_MIRROR_POT_3 },
    { RC_SPIRIT_TEMPLE_MQ_BIG_MIRROR_POT_4, RAND_INF_SPIRIT_TEMPLE_MQ_BIG_MIRROR_POT_4 },
    { RC_SPIRIT_TEMPLE_MQ_BEFORE_MIRROR_POT_1, RAND_INF_SPIRIT_TEMPLE_MQ_BEFORE_MIRROR_POT_1 },
    { RC_SPIRIT_TEMPLE_MQ_BEFORE_MIRROR_POT_2, RAND_INF_SPIRIT_TEMPLE_MQ_BEFORE_MIRROR_POT_2 },
    { RC_SPIRIT_TEMPLE_MQ_EARLY_ADULT_POT_1, RAND_INF_SPIRIT_TEMPLE_MQ_EARLY_ADULT_POT_1 },
    { RC_SPIRIT_TEMPLE_MQ_EARLY_ADULT_POT_2, RAND_INF_SPIRIT_TEMPLE_MQ_EARLY_ADULT_POT_2 },
    { RC_WATER_TEMPLE_MQ_LIZALFOS_HALLWAY_WEST_POT, RAND_INF_WATER_TEMPLE_MQ_CENTRAL_GATE_POT_1 },
    { RC_WATER_TEMPLE_MQ_LIZALFOS_HALLWAY_SOUTH_POT, RAND_INF_WATER_TEMPLE_MQ_CENTRAL_GATE_POT_2 },
    { RC_WATER_TEMPLE_MQ_LIZALFOS_HALLWAY_SE_POT, RAND_INF_WATER_TEMPLE_MQ_CENTRAL_GATE_POT_3 },
    { RC_WATER_TEMPLE_MQ_LIZALFOS_CAGE_SOUTH_POT, RAND_INF_WATER_TEMPLE_MQ_CENTRAL_GATE_POT_4 },
    { RC_WATER_TEMPLE_MQ_LIZALFOS_CAGE_NORTH_POT, RAND_INF_WATER_TEMPLE_MQ_CENTRAL_GATE_POT_5 },
    { RC_WATER_TEMPLE_MQ_STORAGE_ROOM_A_POT_1, RAND_INF_WATER_TEMPLE_MQ_STORAGE_ROOM_A_POT_1 },
    { RC_WATER_TEMPLE_MQ_STORAGE_ROOM_A_POT_2, RAND_INF_WATER_TEMPLE_MQ_STORAGE_ROOM_A_POT_2 },
    { RC_WATER_TEMPLE_MQ_STORAGE_ROOM_A_POT_3, RAND_INF_WATER_TEMPLE_MQ_STORAGE_ROOM_A_POT_3 },
    { RC_WATER_TEMPLE_MQ_BEFORE_DARK_LINK_POT_1, RAND_INF_WATER_TEMPLE_MQ_BEFORE_DARK_LINK_POT_1 },
    { RC_WATER_TEMPLE_MQ_BEFORE_DARK_LINK_POT_2, RAND_INF_WATER_TEMPLE_MQ_BEFORE_DARK_LINK_POT_2 },
    { RC_WATER_TEMPLE_MQ_STALFOS_PIT_MIDDLE_POT, RAND_INF_WATER_TEMPLE_MQ_BEFORE_DARK_LINK_POT_3 },
    { RC_WATER_TEMPLE_MQ_STALFOS_PIT_SOUTH_POT, RAND_INF_WATER_TEMPLE_MQ_BEFORE_DARK_LINK_POT_4 },
    { RC_WATER_TEMPLE_MQ_STALFOS_PIT_NORTH_POT, RAND_INF_WATER_TEMPLE_MQ_BEFORE_DARK_LINK_POT_5 },
    { RC_WATER_TEMPLE_MQ_AFTER_DARK_LINK_POT_1, RAND_INF_WATER_TEMPLE_MQ_AFTER_DARK_LINK_POT_1 },
    { RC_WATER_TEMPLE_MQ_AFTER_DARK_LINK_POT_2, RAND_INF_WATER_TEMPLE_MQ_AFTER_DARK_LINK_POT_2 },
    { RC_WATER_TEMPLE_MQ_RIVER_POT_1, RAND_INF_WATER_TEMPLE_MQ_RIVER_POT_1 },
    { RC_WATER_TEMPLE_MQ_RIVER_POT_2, RAND_INF_WATER_TEMPLE_MQ_RIVER_POT_2 },
    { RC_WATER_TEMPLE_MQ_MINI_DODONGO_POT_1, RAND_INF_WATER_TEMPLE_MQ_MINI_DODONGO_POT_1 },
    { RC_WATER_TEMPLE_MQ_MINI_DODONGO_POT_2, RAND_INF_WATER_TEMPLE_MQ_MINI_DODONGO_POT_2 },
    { RC_WATER_TEMPLE_MQ_STORAGE_ROOM_B_POT_1, RAND_INF_WATER_TEMPLE_MQ_STORAGE_ROOM_B_POT_1 },
    { RC_WATER_TEMPLE_MQ_STORAGE_ROOM_B_POT_2, RAND_INF_WATER_TEMPLE_MQ_STORAGE_ROOM_B_POT_2 },
    { RC_WATER_TEMPLE_MQ_GS_STORAGE_ROOM_POT_1, RAND_INF_WATER_TEMPLE_MQ_GS_STORAGE_ROOM_POT_1 },
    { RC_WATER_TEMPLE_MQ_GS_STORAGE_ROOM_POT_2, RAND_INF_WATER_TEMPLE_MQ_GS_STORAGE_ROOM_POT_2 },
    { RC_WATER_TEMPLE_MQ_GS_STORAGE_ROOM_POT_3, RAND_INF_WATER_TEMPLE_MQ_GS_STORAGE_ROOM_POT_3 },
    { RC_WATER_TEMPLE_MQ_LOWER_TORCHES_POT_1, RAND_INF_WATER_TEMPLE_MQ_LOWER_TORCHES_POT_1 },
    { RC_WATER_TEMPLE_MQ_LOWER_TORCHES_POT_2, RAND_INF_WATER_TEMPLE_MQ_LOWER_TORCHES_POT_2 },
    { RC_WATER_TEMPLE_MQ_LOWEST_GS_POT_1, RAND_INF_WATER_TEMPLE_MQ_LOWEST_GS_POT_1 },
    { RC_WATER_TEMPLE_MQ_LOWEST_GS_POT_2, RAND_INF_WATER_TEMPLE_MQ_LOWEST_GS_POT_2 },
    { RC_WATER_TEMPLE_MQ_LOWEST_GS_POT_3, RAND_INF_WATER_TEMPLE_MQ_LOWEST_GS_POT_3 },
    { RC_WATER_TEMPLE_MQ_LOWEST_GS_POT_4, RAND_INF_WATER_TEMPLE_MQ_LOWEST_GS_POT_4 },
    { RC_WATER_TEMPLE_MQ_BOSS_KEY_POT, RAND_INF_WATER_TEMPLE_MQ_BOSS_KEY_POT },
    { RC_GERUDO_TRAINING_GROUND_MQ_LOBBY_LEFT_POT_1, RAND_INF_GERUDO_TRAINING_GROUND_MQ_LOBBY_LEFT_POT_1 },
    { RC_GERUDO_TRAINING_GROUND_MQ_LOBBY_LEFT_POT_2, RAND_INF_GERUDO_TRAINING_GROUND_MQ_LOBBY_LEFT_POT_2 },
    { RC_GERUDO_TRAINING_GROUND_MQ_LOBBY_RIGHT_POT_1, RAND_INF_GERUDO_TRAINING_GROUND_MQ_LOBBY_RIGHT_POT_1 },
    { RC_GERUDO_TRAINING_GROUND_MQ_LOBBY_RIGHT_POT_2, RAND_INF_GERUDO_TRAINING_GROUND_MQ_LOBBY_RIGHT_POT_2 },
    // Crates
    {
        RC_GV_FREESTANDING_POH_CRATE,
        RAND_INF_GV_FREESTANDING_POH_CRATE,
    },
    {
        RC_GV_NEAR_COW_CRATE,
        RAND_INF_GV_NEAR_COW_CRATE,
    },
    {
        RC_GV_CRATE_BRIDGE_1,
        RAND_INF_GV_CRATE_BRIDGE_1,
    },
    {
        RC_GV_CRATE_BRIDGE_2,
        RAND_INF_GV_CRATE_BRIDGE_2,
    },
    {
        RC_GV_CRATE_BRIDGE_3,
        RAND_INF_GV_CRATE_BRIDGE_3,
    },
    {
        RC_GV_CRATE_BRIDGE_4,
        RAND_INF_GV_CRATE_BRIDGE_4,
    },
    {
        RC_GF_ABOVE_JAIL_CRATE,
        RAND_INF_GF_ABOVE_JAIL_CRATE,
    },
    {
        RC_GF_SOUTHMOST_CENTER_CRATE,
        RAND_INF_GF_SOUTHMOST_CENTER_CRATE,
    },
    {
        RC_GF_MID_SOUTH_CENTER_CRATE,
        RAND_INF_GF_MID_SOUTH_CENTER_CRATE,
    },
    {
        RC_GF_MID_NORTH_CENTER_CRATE,
        RAND_INF_GF_MID_NORTH_CENTER_CRATE,
    },
    {
        RC_GF_NORTHMOST_CENTER_CRATE,
        RAND_INF_GF_NORTHMOST_CENTER_CRATE,
    },
    {
        RC_GF_OUTSKIRTS_NE_CRATE,
        RAND_INF_GF_OUTSKIRTS_NE_CRATE,
    },
    {
        RC_GF_OUTSKIRTS_NW_CRATE,
        RAND_INF_GF_OUTSKIRTS_NW_CRATE,
    },
    {
        RC_GF_HBA_RANGE_CRATE_1,
        RAND_INF_GF_HBA_RANGE_CRATE_1,
    },
    {
        RC_GF_HBA_RANGE_CRATE_2,
        RAND_INF_GF_HBA_RANGE_CRATE_2,
    },
    {
        RC_GF_HBA_RANGE_CRATE_3,
        RAND_INF_GF_HBA_RANGE_CRATE_3,
    },
    {
        RC_GF_HBA_RANGE_CRATE_4,
        RAND_INF_GF_HBA_RANGE_CRATE_4,
    },
    {
        RC_GF_HBA_RANGE_CRATE_5,
        RAND_INF_GF_HBA_RANGE_CRATE_5,
    },
    {
        RC_GF_HBA_RANGE_CRATE_6,
        RAND_INF_GF_HBA_RANGE_CRATE_6,
    },
    {
        RC_GF_HBA_RANGE_CRATE_7,
        RAND_INF_GF_HBA_RANGE_CRATE_7,
    },
    {
        RC_GF_HBA_CANOPY_EAST_CRATE,
        RAND_INF_GF_HBA_CANOPY_EAST_CRATE,
    },
    {
        RC_GF_HBA_CANOPY_WEST_CRATE,
        RAND_INF_GF_HBA_CANOPY_WEST_CRATE,
    },
    {
        RC_GF_NORTH_TARGET_EAST_CRATE,
        RAND_INF_GF_NORTH_TARGET_EAST_CRATE,
    },
    {
        RC_GF_NORTH_TARGET_WEST_CRATE,
        RAND_INF_GF_NORTH_TARGET_WEST_CRATE,
    },
    {
        RC_GF_NORTH_TARGET_CHILD_CRATE,
        RAND_INF_GF_NORTH_TARGET_CHILD_CRATE,
    },
    {
        RC_GF_SOUTH_TARGET_EAST_CRATE,
        RAND_INF_GF_SOUTH_TARGET_EAST_CRATE,
    },
    {
        RC_GF_SOUTH_TARGET_WEST_CRATE,
        RAND_INF_GF_SOUTH_TARGET_WEST_CRATE,
    },
    {
        RC_TH_NEAR_KITCHEN_LEFTMOST_CRATE,
        RAND_INF_TH_NEAR_KITCHEN_LEFTMOST_CRATE,
    },
    {
        RC_TH_NEAR_KITCHEN_MID_LEFT_CRATE,
        RAND_INF_TH_NEAR_KITCHEN_MID_LEFT_CRATE,
    },
    {
        RC_TH_NEAR_KITCHEN_MID_RIGHT_CRATE,
        RAND_INF_TH_NEAR_KITCHEN_MID_RIGHT_CRATE,
    },
    {
        RC_TH_NEAR_KITCHEN_RIGHTMOST_CRATE,
        RAND_INF_TH_NEAR_KITCHEN_RIGHTMOST_CRATE,
    },
    {
        RC_TH_KITCHEN_CRATE,
        RAND_INF_TH_KITCHEN_CRATE,
    },
    {
        RC_TH_BREAK_HALLWAY_OUTER_CRATE,
        RAND_INF_TH_BREAK_HALLWAY_OUTER_CRATE,
    },
    {
        RC_TH_BREAK_HALLWAY_INNER_CRATE,
        RAND_INF_TH_BREAK_HALLWAY_INNER_CRATE,
    },
    {
        RC_TH_BREAK_ROOM_RIGHT_CRATE,
        RAND_INF_TH_BREAK_ROOM_RIGHT_CRATE,
    },
    {
        RC_TH_BREAK_ROOM_LEFT_CRATE,
        RAND_INF_TH_BREAK_ROOM_LEFT_CRATE,
    },
    {
        RC_TH_1_TORCH_CELL_CRATE,
        RAND_INF_TH_1_TORCH_CELL_CRATE,
    },
    {
        RC_TH_DEAD_END_CELL_CRATE,
        RAND_INF_TH_DEAD_END_CELL_CRATE,
    },
    {
        RC_TH_DOUBLE_CELL_LEFT_CRATE,
        RAND_INF_TH_DOUBLE_CELL_LEFT_CRATE,
    },
    {
        RC_TH_DOUBLE_CELL_RIGHT_CRATE,
        RAND_INF_TH_DOUBLE_CELL_RIGHT_CRATE,
    },
    {
        RC_HW_BEFORE_QUICKSAND_CRATE,
        RAND_INF_HW_BEFORE_QUICKSAND_CRATE,
    },
    {
        RC_HW_AFTER_QUICKSAND_CRATE_1,
        RAND_INF_HW_AFTER_QUICKSAND_CRATE_1,
    },
    {
        RC_HW_AFTER_QUICKSAND_CRATE_2,
        RAND_INF_HW_AFTER_QUICKSAND_CRATE_2,
    },
    {
        RC_HW_AFTER_QUICKSAND_CRATE_3,
        RAND_INF_HW_AFTER_QUICKSAND_CRATE_3,
    },
    {
        RC_HW_NEAR_COLOSSUS_CRATE,
        RAND_INF_HW_NEAR_COLOSSUS_CRATE,
    },
    {
        RC_MK_NEAR_BAZAAR_CRATE_1,
        RAND_INF_MK_NEAR_BAZAAR_CRATE_1,
    },
    {
        RC_MK_NEAR_BAZAAR_CRATE_2,
        RAND_INF_MK_NEAR_BAZAAR_CRATE_2,
    },
    {
        RC_MK_SHOOTING_GALLERY_CRATE_1,
        RAND_INF_MK_SHOOTING_GALLERY_CRATE_1,
    },
    {
        RC_MK_SHOOTING_GALLERY_CRATE_2,
        RAND_INF_MK_SHOOTING_GALLERY_CRATE_2,
    },
    {
        RC_MK_LOST_DOG_HOUSE_CRATE,
        RAND_INF_MK_LOST_DOG_HOUSE_CRATE,
    },
    {
        RC_MK_GUARD_HOUSE_CRATE_1,
        RAND_INF_MK_GUARD_HOUSE_CRATE_1,
    },
    {
        RC_MK_GUARD_HOUSE_CRATE_2,
        RAND_INF_MK_GUARD_HOUSE_CRATE_2,
    },
    {
        RC_MK_GUARD_HOUSE_CRATE_3,
        RAND_INF_MK_GUARD_HOUSE_CRATE_3,
    },
    {
        RC_MK_GUARD_HOUSE_CRATE_4,
        RAND_INF_MK_GUARD_HOUSE_CRATE_4,
    },
    {
        RC_MK_GUARD_HOUSE_CRATE_5,
        RAND_INF_MK_GUARD_HOUSE_CRATE_5,
    },
    {
        RC_KAK_NEAR_OPEN_GROTTO_ADULT_CRATE_1,
        RAND_INF_KAK_NEAR_OPEN_GROTTO_ADULT_CRATE_1,
    },
    {
        RC_KAK_NEAR_OPEN_GROTTO_ADULT_CRATE_2,
        RAND_INF_KAK_NEAR_OPEN_GROTTO_ADULT_CRATE_2,
    },
    {
        RC_KAK_NEAR_OPEN_GROTTO_ADULT_CRATE_3,
        RAND_INF_KAK_NEAR_OPEN_GROTTO_ADULT_CRATE_3,
    },
    {
        RC_KAK_NEAR_OPEN_GROTTO_ADULT_CRATE_4,
        RAND_INF_KAK_NEAR_OPEN_GROTTO_ADULT_CRATE_4,
    },
    {
        RC_KAK_NEAR_POTION_SHOP_ADULT_CRATE,
        RAND_INF_KAK_NEAR_POTION_SHOP_ADULT_CRATE,
    },
    {
        RC_KAK_NEAR_SHOOTING_GALLERY_ADULT_CRATE,
        RAND_INF_KAK_NEAR_SHOOTING_GALLERY_ADULT_CRATE,
    },
    {
        RC_KAK_NEAR_BOARDING_HOUSE_ADULT_CRATE_1,
        RAND_INF_KAK_NEAR_BOARDING_HOUSE_ADULT_CRATE_1,
    },
    {
        RC_KAK_NEAR_BOARDING_HOUSE_ADULT_CRATE_2,
        RAND_INF_KAK_NEAR_BOARDING_HOUSE_ADULT_CRATE_2,
    },
    {
        RC_KAK_NEAR_IMPAS_HOUSE_ADULT_CRATE_1,
        RAND_INF_KAK_NEAR_IMPAS_HOUSE_ADULT_CRATE_1,
    },
    {
        RC_KAK_NEAR_IMPAS_HOUSE_ADULT_CRATE_2,
        RAND_INF_KAK_NEAR_IMPAS_HOUSE_ADULT_CRATE_2,
    },
    {
        RC_KAK_NEAR_BAZAAR_ADULT_CRATE_1,
        RAND_INF_KAK_NEAR_BAZAAR_ADULT_CRATE_1,
    },
    {
        RC_KAK_NEAR_BAZAAR_ADULT_CRATE_2,
        RAND_INF_KAK_NEAR_BAZAAR_ADULT_CRATE_2,
    },
    {
        RC_KAK_BEHIND_GS_HOUSE_ADULT_CRATE,
        RAND_INF_KAK_BEHIND_GS_HOUSE_ADULT_CRATE,
    },
    {
        RC_KAK_NEAR_GY_CHILD_CRATE,
        RAND_INF_KAK_NEAR_GY_CHILD_CRATE,
    },
    {
        RC_KAK_NEAR_WINDMILL_CHILD_CRATE,
        RAND_INF_KAK_NEAR_WINDMILL_CHILD_CRATE,
    },
    {
        RC_KAK_NEAR_FENCE_CHILD_CRATE,
        RAND_INF_KAK_NEAR_FENCE_CHILD_CRATE,
    },
    {
        RC_KAK_NEAR_BOARDING_HOUSE_CHILD_CRATE,
        RAND_INF_KAK_NEAR_BOARDING_HOUSE_CHILD_CRATE,
    },
    {
        RC_KAK_NEAR_BAZAAR_CHILD_CRATE,
        RAND_INF_KAK_NEAR_BAZAAR_CHILD_CRATE,
    },
    {
        RC_GRAVEYARD_CRATE,
        RAND_INF_GRAVEYARD_CRATE,
    },
    {
        RC_GC_MAZE_CRATE,
        RAND_INF_GC_MAZE_CRATE,
    },
    {
        RC_DMC_CRATE,
        RAND_INF_DMC_CRATE,
    },
    {
        RC_LLR_NEAR_TREE_CRATE,
        RAND_INF_LLR_NEAR_TREE_CRATE,
    },
    {
        RC_LH_LAB_CRATE,
        RAND_INF_LH_LAB_CRATE,
    },

    {
        RC_DEKU_TREE_MQ_LOBBY_CRATE,
        RAND_INF_DEKU_TREE_MQ_LOBBY_CRATE,
    },
    {
        RC_DEKU_TREE_MQ_SLINGSHOT_ROOM_CRATE_1,
        RAND_INF_DEKU_TREE_MQ_SLINGSHOT_ROOM_CRATE_1,
    },
    {
        RC_DEKU_TREE_MQ_SLINGSHOT_ROOM_CRATE_2,
        RAND_INF_DEKU_TREE_MQ_SLINGSHOT_ROOM_CRATE_2,
    },
    {
        RC_DODONGOS_CAVERN_MQ_POE_ROOM_CRATE_1,
        RAND_INF_DODONGOS_CAVERN_MQ_POE_ROOM_CRATE_1,
    },
    {
        RC_DODONGOS_CAVERN_MQ_POE_ROOM_CRATE_2,
        RAND_INF_DODONGOS_CAVERN_MQ_POE_ROOM_CRATE_2,
    },
    {
        RC_DODONGOS_CAVERN_MQ_POE_ROOM_CRATE_3,
        RAND_INF_DODONGOS_CAVERN_MQ_POE_ROOM_CRATE_3,
    },
    {
        RC_DODONGOS_CAVERN_MQ_POE_ROOM_CRATE_4,
        RAND_INF_DODONGOS_CAVERN_MQ_POE_ROOM_CRATE_4,
    },
    {
        RC_DODONGOS_CAVERN_MQ_POE_ROOM_CRATE_5,
        RAND_INF_DODONGOS_CAVERN_MQ_POE_ROOM_CRATE_5,
    },
    {
        RC_DODONGOS_CAVERN_MQ_POE_ROOM_CRATE_6,
        RAND_INF_DODONGOS_CAVERN_MQ_POE_ROOM_CRATE_6,
    },
    {
        RC_DODONGOS_CAVERN_MQ_POE_ROOM_CRATE_7,
        RAND_INF_DODONGOS_CAVERN_MQ_POE_ROOM_CRATE_7,
    },
    {
        RC_DODONGOS_CAVERN_MQ_POE_ROOM_CRATE_8,
        RAND_INF_DODONGOS_CAVERN_MQ_POE_ROOM_CRATE_8,
    },
    {
        RC_DODONGOS_CAVERN_MQ_STAIRCASE_LOWER_CRATE_1,
        RAND_INF_DODONGOS_CAVERN_MQ_STAIRCASE_LOWER_CRATE_1,
    },
    {
        RC_DODONGOS_CAVERN_MQ_STAIRCASE_LOWER_CRATE_2,
        RAND_INF_DODONGOS_CAVERN_MQ_STAIRCASE_LOWER_CRATE_2,
    },
    {
        RC_DODONGOS_CAVERN_MQ_STAIRCASE_UPPER_CRATE_1,
        RAND_INF_DODONGOS_CAVERN_MQ_STAIRCASE_UPPER_CRATE_1,
    },
    {
        RC_DODONGOS_CAVERN_MQ_STAIRCASE_UPPER_CRATE_2,
        RAND_INF_DODONGOS_CAVERN_MQ_STAIRCASE_UPPER_CRATE_2,
    },
    {
        RC_DODONGOS_CAVERN_MQ_STAIRCASE_UPPER_CRATE_3,
        RAND_INF_DODONGOS_CAVERN_MQ_STAIRCASE_UPPER_CRATE_3,
    },
    {
        RC_DODONGOS_CAVERN_MQ_STAIRCASE_UPPER_CRATE_4,
        RAND_INF_DODONGOS_CAVERN_MQ_STAIRCASE_UPPER_CRATE_4,
    },
    {
        RC_DODONGOS_CAVERN_MQ_TWO_FLAMES_CRATE_1,
        RAND_INF_DODONGOS_CAVERN_MQ_TWO_FLAMES_CRATE_1,
    },
    {
        RC_DODONGOS_CAVERN_MQ_TWO_FLAMES_CRATE_2,
        RAND_INF_DODONGOS_CAVERN_MQ_TWO_FLAMES_CRATE_2,
    },
    {
        RC_DODONGOS_CAVERN_MQ_LARVAE_ROOM_CRATE_1,
        RAND_INF_DODONGOS_CAVERN_MQ_LARVAE_ROOM_CRATE_1,
    },
    {
        RC_DODONGOS_CAVERN_MQ_LARVAE_ROOM_CRATE_2,
        RAND_INF_DODONGOS_CAVERN_MQ_LARVAE_ROOM_CRATE_2,
    },
    {
        RC_DODONGOS_CAVERN_MQ_LARVAE_ROOM_CRATE_3,
        RAND_INF_DODONGOS_CAVERN_MQ_LARVAE_ROOM_CRATE_3,
    },
    {
        RC_DODONGOS_CAVERN_MQ_LARVAE_ROOM_CRATE_4,
        RAND_INF_DODONGOS_CAVERN_MQ_LARVAE_ROOM_CRATE_4,
    },
    {
        RC_DODONGOS_CAVERN_MQ_LARVAE_ROOM_CRATE_5,
        RAND_INF_DODONGOS_CAVERN_MQ_LARVAE_ROOM_CRATE_5,
    },
    {
        RC_DODONGOS_CAVERN_MQ_LARVAE_ROOM_CRATE_6,
        RAND_INF_DODONGOS_CAVERN_MQ_LARVAE_ROOM_CRATE_6,
    },
    {
        RC_FIRE_TEMPLE_MQ_OUTSIDE_BOSS_CRATE_1,
        RAND_INF_FIRE_TEMPLE_MQ_OUTSIDE_BOSS_CRATE_1,
    },
    {
        RC_FIRE_TEMPLE_MQ_OUTSIDE_BOSS_CRATE_2,
        RAND_INF_FIRE_TEMPLE_MQ_OUTSIDE_BOSS_CRATE_2,
    },
    {
        RC_FIRE_TEMPLE_MQ_OUTSIDE_BOSS_CRATE_3,
        RAND_INF_FIRE_TEMPLE_MQ_OUTSIDE_BOSS_CRATE_3,
    },
    {
        RC_FIRE_TEMPLE_MQ_OUTSIDE_BOSS_CRATE_4,
        RAND_INF_FIRE_TEMPLE_MQ_OUTSIDE_BOSS_CRATE_4,
    },
    {
        RC_FIRE_TEMPLE_MQ_OUTSIDE_BOSS_CRATE_5,
        RAND_INF_FIRE_TEMPLE_MQ_OUTSIDE_BOSS_CRATE_5,
    },
    {
        RC_FIRE_TEMPLE_MQ_OUTSIDE_BOSS_CRATE_6,
        RAND_INF_FIRE_TEMPLE_MQ_OUTSIDE_BOSS_CRATE_6,
    },
    {
        RC_FIRE_TEMPLE_MQ_SHORTCUT_CRATE_1,
        RAND_INF_FIRE_TEMPLE_MQ_SHORTCUT_CRATE_1,
    },
    {
        RC_FIRE_TEMPLE_MQ_SHORTCUT_CRATE_2,
        RAND_INF_FIRE_TEMPLE_MQ_SHORTCUT_CRATE_2,
    },
    {
        RC_FIRE_TEMPLE_MQ_SHORTCUT_CRATE_3,
        RAND_INF_FIRE_TEMPLE_MQ_SHORTCUT_CRATE_3,
    },
    {
        RC_FIRE_TEMPLE_MQ_SHORTCUT_CRATE_4,
        RAND_INF_FIRE_TEMPLE_MQ_SHORTCUT_CRATE_4,
    },
    {
        RC_FIRE_TEMPLE_MQ_SHORTCUT_CRATE_5,
        RAND_INF_FIRE_TEMPLE_MQ_SHORTCUT_CRATE_5,
    },
    {
        RC_FIRE_TEMPLE_MQ_SHORTCUT_CRATE_6,
        RAND_INF_FIRE_TEMPLE_MQ_SHORTCUT_CRATE_6,
    },
    {
        RC_FIRE_TEMPLE_MQ_LIZALFOS_MAZE_LOWER_CRATE_1,
        RAND_INF_FIRE_TEMPLE_MQ_LIZALFOS_MAZE_LOWER_CRATE_1,
    },
    {
        RC_FIRE_TEMPLE_MQ_LIZALFOS_MAZE_LOWER_CRATE_2,
        RAND_INF_FIRE_TEMPLE_MQ_LIZALFOS_MAZE_LOWER_CRATE_2,
    },
    {
        RC_FIRE_TEMPLE_MQ_LIZALFOS_MAZE_LOWER_CRATE_3,
        RAND_INF_FIRE_TEMPLE_MQ_LIZALFOS_MAZE_LOWER_CRATE_3,
    },
    {
        RC_FIRE_TEMPLE_MQ_LIZALFOS_MAZE_UPPER_CRATE_1,
        RAND_INF_FIRE_TEMPLE_MQ_LIZALFOS_MAZE_UPPER_CRATE_1,
    },
    {
        RC_FIRE_TEMPLE_MQ_LIZALFOS_MAZE_UPPER_CRATE_2,
        RAND_INF_FIRE_TEMPLE_MQ_LIZALFOS_MAZE_UPPER_CRATE_2,
    },
    {
        RC_FIRE_TEMPLE_MQ_LIZALFOS_MAZE_UPPER_CRATE_3,
        RAND_INF_FIRE_TEMPLE_MQ_LIZALFOS_MAZE_UPPER_CRATE_3,
    },
    {
        RC_FIRE_TEMPLE_MQ_LAVA_TORCH_CRATE_1,
        RAND_INF_FIRE_TEMPLE_MQ_LAVA_TORCH_CRATE_1,
    },
    {
        RC_FIRE_TEMPLE_MQ_LAVA_TORCH_CRATE_2,
        RAND_INF_FIRE_TEMPLE_MQ_LAVA_TORCH_CRATE_2,
    },
    {
        RC_FIRE_TEMPLE_MQ_LAVA_TORCH_CRATE_3,
        RAND_INF_FIRE_TEMPLE_MQ_LAVA_TORCH_CRATE_3,
    },
    {
        RC_FIRE_TEMPLE_MQ_LAVA_TORCH_CRATE_4,
        RAND_INF_FIRE_TEMPLE_MQ_LAVA_TORCH_CRATE_4,
    },
    {
        RC_FIRE_TEMPLE_MQ_LAVA_TORCH_CRATE_5,
        RAND_INF_FIRE_TEMPLE_MQ_LAVA_TORCH_CRATE_5,
    },
    {
        RC_WATER_TEMPLE_MQ_CENTRAL_PILLAR_UPPER_CRATE_1,
        RAND_INF_WATER_TEMPLE_MQ_CENTRAL_PILLAR_UPPER_CRATE_1,
    },
    {
        RC_WATER_TEMPLE_MQ_CENTRAL_PILLAR_UPPER_CRATE_2,
        RAND_INF_WATER_TEMPLE_MQ_CENTRAL_PILLAR_UPPER_CRATE_2,
    },
    {
        RC_WATER_TEMPLE_MQ_CENTRAL_PILLAR_LOWER_CRATE_1,
        RAND_INF_WATER_TEMPLE_MQ_CENTRAL_PILLAR_LOWER_CRATE_1,
    },
    {
        RC_WATER_TEMPLE_MQ_CENTRAL_PILLAR_LOWER_CRATE_2,
        RAND_INF_WATER_TEMPLE_MQ_CENTRAL_PILLAR_LOWER_CRATE_2,
    },
    {
        RC_WATER_TEMPLE_MQ_CENTRAL_PILLAR_LOWER_CRATE_3,
        RAND_INF_WATER_TEMPLE_MQ_CENTRAL_PILLAR_LOWER_CRATE_3,
    },
    {
        RC_WATER_TEMPLE_MQ_CENTRAL_PILLAR_LOWER_CRATE_4,
        RAND_INF_WATER_TEMPLE_MQ_CENTRAL_PILLAR_LOWER_CRATE_4,
    },
    {
        RC_WATER_TEMPLE_MQ_CENTRAL_PILLAR_LOWER_CRATE_5,
        RAND_INF_WATER_TEMPLE_MQ_CENTRAL_PILLAR_LOWER_CRATE_5,
    },
    {
        RC_WATER_TEMPLE_MQ_CENTRAL_PILLAR_LOWER_CRATE_6,
        RAND_INF_WATER_TEMPLE_MQ_CENTRAL_PILLAR_LOWER_CRATE_6,
    },
    {
        RC_WATER_TEMPLE_MQ_CENTRAL_PILLAR_LOWER_CRATE_7,
        RAND_INF_WATER_TEMPLE_MQ_CENTRAL_PILLAR_LOWER_CRATE_7,
    },
    {
        RC_WATER_TEMPLE_MQ_CENTRAL_PILLAR_LOWER_CRATE_8,
        RAND_INF_WATER_TEMPLE_MQ_CENTRAL_PILLAR_LOWER_CRATE_8,
    },
    {
        RC_WATER_TEMPLE_MQ_CENTRAL_PILLAR_LOWER_CRATE_9,
        RAND_INF_WATER_TEMPLE_MQ_CENTRAL_PILLAR_LOWER_CRATE_9,
    },
    {
        RC_WATER_TEMPLE_MQ_CENTRAL_PILLAR_LOWER_CRATE_10,
        RAND_INF_WATER_TEMPLE_MQ_CENTRAL_PILLAR_LOWER_CRATE_10,
    },
    {
        RC_WATER_TEMPLE_MQ_CENTRAL_PILLAR_LOWER_CRATE_11,
        RAND_INF_WATER_TEMPLE_MQ_CENTRAL_PILLAR_LOWER_CRATE_11,
    },
    {
        RC_WATER_TEMPLE_MQ_CENTRAL_PILLAR_LOWER_CRATE_12,
        RAND_INF_WATER_TEMPLE_MQ_CENTRAL_PILLAR_LOWER_CRATE_12,
    },
    {
        RC_WATER_TEMPLE_MQ_CENTRAL_PILLAR_LOWER_CRATE_13,
        RAND_INF_WATER_TEMPLE_MQ_CENTRAL_PILLAR_LOWER_CRATE_13,
    },
    {
        RC_WATER_TEMPLE_MQ_CENTRAL_PILLAR_LOWER_CRATE_14,
        RAND_INF_WATER_TEMPLE_MQ_CENTRAL_PILLAR_LOWER_CRATE_14,
    },
    {
        RC_WATER_TEMPLE_MQ_LIZALFOS_HALLWAY_CRATE_1,
        RAND_INF_WATER_TEMPLE_MQ_LIZALFOS_HALLWAY_CRATE_1,
    },
    {
        RC_WATER_TEMPLE_MQ_LIZALFOS_HALLWAY_CRATE_2,
        RAND_INF_WATER_TEMPLE_MQ_LIZALFOS_HALLWAY_CRATE_2,
    },
    {
        RC_WATER_TEMPLE_MQ_LIZALFOS_HALLWAY_CRATE_3,
        RAND_INF_WATER_TEMPLE_MQ_LIZALFOS_HALLWAY_CRATE_3,
    },
    {
        RC_WATER_TEMPLE_MQ_LIZALFOS_HALLWAY_ROOM_CRATE_1,
        RAND_INF_WATER_TEMPLE_MQ_LIZALFOS_HALLWAY_ROOM_CRATE_1,
    },
    {
        RC_WATER_TEMPLE_MQ_LIZALFOS_HALLWAY_ROOM_CRATE_2,
        RAND_INF_WATER_TEMPLE_MQ_LIZALFOS_HALLWAY_ROOM_CRATE_2,
    },
    {
        RC_WATER_TEMPLE_MQ_LIZALFOS_HALLWAY_ROOM_CRATE_3,
        RAND_INF_WATER_TEMPLE_MQ_LIZALFOS_HALLWAY_ROOM_CRATE_3,
    },
    {
        RC_WATER_TEMPLE_MQ_LIZALFOS_HALLWAY_ROOM_CRATE_4,
        RAND_INF_WATER_TEMPLE_MQ_LIZALFOS_HALLWAY_ROOM_CRATE_4,
    },
    {
        RC_WATER_TEMPLE_MQ_LIZALFOS_HALLWAY_ROOM_CRATE_5,
        RAND_INF_WATER_TEMPLE_MQ_LIZALFOS_HALLWAY_ROOM_CRATE_5,
    },
    {
        RC_WATER_TEMPLE_MQ_LIZALFOS_HALLWAY_GATE_CRATE_1,
        RAND_INF_WATER_TEMPLE_MQ_LIZALFOS_HALLWAY_GATE_CRATE_1,
    },
    {
        RC_WATER_TEMPLE_MQ_LIZALFOS_HALLWAY_GATE_CRATE_2,
        RAND_INF_WATER_TEMPLE_MQ_LIZALFOS_HALLWAY_GATE_CRATE_2,
    },
    {
        RC_WATER_TEMPLE_MQ_STORAGE_ROOM_A_CRATE_1,
        RAND_INF_WATER_TEMPLE_MQ_STORAGE_ROOM_A_CRATE_1,
    },
    {
        RC_WATER_TEMPLE_MQ_STORAGE_ROOM_A_CRATE_2,
        RAND_INF_WATER_TEMPLE_MQ_STORAGE_ROOM_A_CRATE_2,
    },
    {
        RC_WATER_TEMPLE_MQ_STORAGE_ROOM_A_CRATE_3,
        RAND_INF_WATER_TEMPLE_MQ_STORAGE_ROOM_A_CRATE_3,
    },
    {
        RC_WATER_TEMPLE_MQ_STORAGE_ROOM_A_CRATE_4,
        RAND_INF_WATER_TEMPLE_MQ_STORAGE_ROOM_A_CRATE_4,
    },
    {
        RC_WATER_TEMPLE_MQ_STORAGE_ROOM_A_CRATE_5,
        RAND_INF_WATER_TEMPLE_MQ_STORAGE_ROOM_A_CRATE_5,
    },
    {
        RC_WATER_TEMPLE_MQ_STORAGE_ROOM_A_CRATE_6,
        RAND_INF_WATER_TEMPLE_MQ_STORAGE_ROOM_A_CRATE_6,
    },
    {
        RC_WATER_TEMPLE_MQ_STORAGE_ROOM_A_CRATE_7,
        RAND_INF_WATER_TEMPLE_MQ_STORAGE_ROOM_A_CRATE_7,
    },
    {
        RC_WATER_TEMPLE_MQ_GS_STORAGE_ROOM_LOWER_CRATE_1,
        RAND_INF_WATER_TEMPLE_MQ_GS_STORAGE_ROOM_LOWER_CRATE_1,
    },
    {
        RC_WATER_TEMPLE_MQ_GS_STORAGE_ROOM_LOWER_CRATE_2,
        RAND_INF_WATER_TEMPLE_MQ_GS_STORAGE_ROOM_LOWER_CRATE_2,
    },
    {
        RC_WATER_TEMPLE_MQ_GS_STORAGE_ROOM_LOWER_CRATE_3,
        RAND_INF_WATER_TEMPLE_MQ_GS_STORAGE_ROOM_LOWER_CRATE_3,
    },
    {
        RC_WATER_TEMPLE_MQ_GS_STORAGE_ROOM_LOWER_CRATE_4,
        RAND_INF_WATER_TEMPLE_MQ_GS_STORAGE_ROOM_LOWER_CRATE_4,
    },
    {
        RC_WATER_TEMPLE_MQ_GS_STORAGE_ROOM_LOWER_CRATE_5,
        RAND_INF_WATER_TEMPLE_MQ_GS_STORAGE_ROOM_LOWER_CRATE_5,
    },
    {
        RC_WATER_TEMPLE_MQ_GS_STORAGE_ROOM_LOWER_CRATE_6,
        RAND_INF_WATER_TEMPLE_MQ_GS_STORAGE_ROOM_LOWER_CRATE_6,
    },
    {
        RC_WATER_TEMPLE_MQ_GS_STORAGE_ROOM_UPPER_CRATE_1,
        RAND_INF_WATER_TEMPLE_MQ_GS_STORAGE_ROOM_UPPER_CRATE_1,
    },
    {
        RC_WATER_TEMPLE_MQ_GS_STORAGE_ROOM_UPPER_CRATE_2,
        RAND_INF_WATER_TEMPLE_MQ_GS_STORAGE_ROOM_UPPER_CRATE_2,
    },
    {
        RC_WATER_TEMPLE_MQ_DRAGON_ROOM_TORCHES_CRATE_1,
        RAND_INF_WATER_TEMPLE_MQ_DRAGON_ROOM_TORCHES_CRATE_1,
    },
    {
        RC_WATER_TEMPLE_MQ_DRAGON_ROOM_TORCHES_CRATE_2,
        RAND_INF_WATER_TEMPLE_MQ_DRAGON_ROOM_TORCHES_CRATE_2,
    },
    {
        RC_WATER_TEMPLE_MQ_DRAGON_ROOM_SUBMERGED_CRATE_1,
        RAND_INF_WATER_TEMPLE_MQ_DRAGON_ROOM_SUBMERGED_CRATE_1,
    },
    {
        RC_WATER_TEMPLE_MQ_DRAGON_ROOM_SUBMERGED_CRATE_2,
        RAND_INF_WATER_TEMPLE_MQ_DRAGON_ROOM_SUBMERGED_CRATE_2,
    },
    {
        RC_WATER_TEMPLE_MQ_DRAGON_ROOM_SUBMERGED_CRATE_3,
        RAND_INF_WATER_TEMPLE_MQ_DRAGON_ROOM_SUBMERGED_CRATE_3,
    },
    {
        RC_WATER_TEMPLE_MQ_DRAGON_ROOM_SUBMERGED_CRATE_4,
        RAND_INF_WATER_TEMPLE_MQ_DRAGON_ROOM_SUBMERGED_CRATE_4,
    },
    {
        RC_WATER_TEMPLE_MQ_DRAGON_ROOM_DOOR_CRATE_1,
        RAND_INF_WATER_TEMPLE_MQ_DRAGON_ROOM_DOOR_CRATE_1,
    },
    {
        RC_WATER_TEMPLE_MQ_DRAGON_ROOM_DOOR_CRATE_2,
        RAND_INF_WATER_TEMPLE_MQ_DRAGON_ROOM_DOOR_CRATE_2,
    },
    {
        RC_WATER_TEMPLE_MQ_BK_ROOM_UPPER_CRATE,
        RAND_INF_WATER_TEMPLE_MQ_BK_ROOM_UPPER_CRATE,
    },
    {
        RC_WATER_TEMPLE_MQ_BK_ROOM_LOWER_CRATE_1,
        RAND_INF_WATER_TEMPLE_MQ_BK_ROOM_LOWER_CRATE_1,
    },
    {
        RC_WATER_TEMPLE_MQ_BK_ROOM_LOWER_CRATE_2,
        RAND_INF_WATER_TEMPLE_MQ_BK_ROOM_LOWER_CRATE_2,
    },
    {
        RC_WATER_TEMPLE_MQ_BK_ROOM_LOWER_CRATE_3,
        RAND_INF_WATER_TEMPLE_MQ_BK_ROOM_LOWER_CRATE_3,
    },
    {
        RC_WATER_TEMPLE_MQ_BK_ROOM_LOWER_CRATE_4,
        RAND_INF_WATER_TEMPLE_MQ_BK_ROOM_LOWER_CRATE_4,
    },
    {
        RC_WATER_TEMPLE_MQ_WHIRLPOOL_FRONT_CRATE_1,
        RAND_INF_WATER_TEMPLE_MQ_WHIRLPOOL_FRONT_CRATE_1,
    },
    {
        RC_WATER_TEMPLE_MQ_WHIRLPOOL_FRONT_CRATE_2,
        RAND_INF_WATER_TEMPLE_MQ_WHIRLPOOL_FRONT_CRATE_2,
    },
    {
        RC_WATER_TEMPLE_MQ_WHIRLPOOL_SUBMERGED_CRATE_1,
        RAND_INF_WATER_TEMPLE_MQ_WHIRLPOOL_SUBMERGED_CRATE_1,
    },
    {
        RC_WATER_TEMPLE_MQ_WHIRLPOOL_SUBMERGED_CRATE_2,
        RAND_INF_WATER_TEMPLE_MQ_WHIRLPOOL_SUBMERGED_CRATE_2,
    },
    {
        RC_WATER_TEMPLE_MQ_WHIRLPOOL_SUBMERGED_CRATE_3,
        RAND_INF_WATER_TEMPLE_MQ_WHIRLPOOL_SUBMERGED_CRATE_3,
    },
    {
        RC_WATER_TEMPLE_MQ_WHIRLPOOL_SUBMERGED_CRATE_4,
        RAND_INF_WATER_TEMPLE_MQ_WHIRLPOOL_SUBMERGED_CRATE_4,
    },
    {
        RC_WATER_TEMPLE_MQ_WHIRLPOOL_SUBMERGED_CRATE_5,
        RAND_INF_WATER_TEMPLE_MQ_WHIRLPOOL_SUBMERGED_CRATE_5,
    },
    {
        RC_WATER_TEMPLE_MQ_WHIRLPOOL_SUBMERGED_CRATE_6,
        RAND_INF_WATER_TEMPLE_MQ_WHIRLPOOL_SUBMERGED_CRATE_6,
    },
    {
        RC_WATER_TEMPLE_MQ_WHIRLPOOL_BEHIND_GATE_CRATE_1,
        RAND_INF_WATER_TEMPLE_MQ_WHIRLPOOL_BEHIND_GATE_CRATE_1,
    },
    {
        RC_WATER_TEMPLE_MQ_WHIRLPOOL_BEHIND_GATE_CRATE_2,
        RAND_INF_WATER_TEMPLE_MQ_WHIRLPOOL_BEHIND_GATE_CRATE_2,
    },
    {
        RC_WATER_TEMPLE_MQ_WHIRLPOOL_BEHIND_GATE_CRATE_3,
        RAND_INF_WATER_TEMPLE_MQ_WHIRLPOOL_BEHIND_GATE_CRATE_3,
    },
    {
        RC_WATER_TEMPLE_MQ_WHIRLPOOL_BEHIND_GATE_CRATE_4,
        RAND_INF_WATER_TEMPLE_MQ_WHIRLPOOL_BEHIND_GATE_CRATE_4,
    },
    {
        RC_WATER_TEMPLE_MQ_DODONGO_ROOM_UPPER_CRATE,
        RAND_INF_WATER_TEMPLE_MQ_DODONGO_ROOM_UPPER_CRATE,
    },
    {
        RC_WATER_TEMPLE_MQ_DODONGO_ROOM_HALL_CRATE,
        RAND_INF_WATER_TEMPLE_MQ_DODONGO_ROOM_HALL_CRATE,
    },
    {
        RC_WATER_TEMPLE_MQ_DODONGO_ROOM_LOWER_CRATE_1,
        RAND_INF_WATER_TEMPLE_MQ_DODONGO_ROOM_LOWER_CRATE_1,
    },
    {
        RC_WATER_TEMPLE_MQ_DODONGO_ROOM_LOWER_CRATE_2,
        RAND_INF_WATER_TEMPLE_MQ_DODONGO_ROOM_LOWER_CRATE_2,
    },
    {
        RC_WATER_TEMPLE_MQ_DODONGO_ROOM_LOWER_CRATE_3,
        RAND_INF_WATER_TEMPLE_MQ_DODONGO_ROOM_LOWER_CRATE_3,
    },
    {
        RC_WATER_TEMPLE_MQ_STORAGE_ROOM_B_CRATE_1,
        RAND_INF_WATER_TEMPLE_MQ_STORAGE_ROOM_B_CRATE_1,
    },
    {
        RC_WATER_TEMPLE_MQ_STORAGE_ROOM_B_CRATE_2,
        RAND_INF_WATER_TEMPLE_MQ_STORAGE_ROOM_B_CRATE_2,
    },
    {
        RC_WATER_TEMPLE_MQ_STORAGE_ROOM_B_CRATE_3,
        RAND_INF_WATER_TEMPLE_MQ_STORAGE_ROOM_B_CRATE_3,
    },
    {
        RC_WATER_TEMPLE_MQ_STORAGE_ROOM_B_CRATE_4,
        RAND_INF_WATER_TEMPLE_MQ_STORAGE_ROOM_B_CRATE_4,
    },
    {
        RC_WATER_TEMPLE_MQ_STORAGE_ROOM_B_CRATE_5,
        RAND_INF_WATER_TEMPLE_MQ_STORAGE_ROOM_B_CRATE_5,
    },
    {
        RC_WATER_TEMPLE_MQ_TRIPLE_TORCH_ROOM_SUBMERGED_CRATE_1,
        RAND_INF_WATER_TEMPLE_MQ_TRIPLE_TORCH_ROOM_SUBMERGED_CRATE_1,
    },
    {
        RC_WATER_TEMPLE_MQ_TRIPLE_TORCH_ROOM_SUBMERGED_CRATE_2,
        RAND_INF_WATER_TEMPLE_MQ_TRIPLE_TORCH_ROOM_SUBMERGED_CRATE_2,
    },
    {
        RC_WATER_TEMPLE_MQ_TRIPLE_TORCH_ROOM_SUBMERGED_CRATE_3,
        RAND_INF_WATER_TEMPLE_MQ_TRIPLE_TORCH_ROOM_SUBMERGED_CRATE_3,
    },
    {
        RC_WATER_TEMPLE_MQ_TRIPLE_TORCH_ROOM_SUBMERGED_CRATE_4,
        RAND_INF_WATER_TEMPLE_MQ_TRIPLE_TORCH_ROOM_SUBMERGED_CRATE_4,
    },
    {
        RC_WATER_TEMPLE_MQ_TRIPLE_TORCH_ROOM_SUBMERGED_CRATE_5,
        RAND_INF_WATER_TEMPLE_MQ_TRIPLE_TORCH_ROOM_SUBMERGED_CRATE_5,
    },
    {
        RC_WATER_TEMPLE_MQ_TRIPLE_TORCH_ROOM_SUBMERGED_CRATE_6,
        RAND_INF_WATER_TEMPLE_MQ_TRIPLE_TORCH_ROOM_SUBMERGED_CRATE_6,
    },
    {
        RC_WATER_TEMPLE_MQ_TRIPLE_TORCH_ROOM_GATE_CRATE_1,
        RAND_INF_WATER_TEMPLE_MQ_TRIPLE_TORCH_ROOM_GATE_CRATE_1,
    },
    {
        RC_WATER_TEMPLE_MQ_TRIPLE_TORCH_ROOM_GATE_CRATE_2,
        RAND_INF_WATER_TEMPLE_MQ_TRIPLE_TORCH_ROOM_GATE_CRATE_2,
    },
    {
        RC_WATER_TEMPLE_MQ_TRIPLE_TORCH_ROOM_GATE_CRATE_3,
        RAND_INF_WATER_TEMPLE_MQ_TRIPLE_TORCH_ROOM_GATE_CRATE_3,
    },
    {
        RC_SPIRIT_TEMPLE_MQ_STATUE_CRATE_1,
        RAND_INF_SPIRIT_TEMPLE_MQ_STATUE_CRATE_1,
    },
    {
        RC_SPIRIT_TEMPLE_MQ_STATUE_CRATE_2,
        RAND_INF_SPIRIT_TEMPLE_MQ_STATUE_CRATE_2,
    },
    {
        RC_SPIRIT_TEMPLE_MQ_BIG_MIRROR_CRATE_1,
        RAND_INF_SPIRIT_TEMPLE_MQ_BIG_MIRROR_CRATE_1,
    },
    {
        RC_SPIRIT_TEMPLE_MQ_BIG_MIRROR_CRATE_2,
        RAND_INF_SPIRIT_TEMPLE_MQ_BIG_MIRROR_CRATE_2,
    },
    {
        RC_SPIRIT_TEMPLE_MQ_BIG_MIRROR_CRATE_3,
        RAND_INF_SPIRIT_TEMPLE_MQ_BIG_MIRROR_CRATE_3,
    },
    {
        RC_SPIRIT_TEMPLE_MQ_BIG_MIRROR_CRATE_4,
        RAND_INF_SPIRIT_TEMPLE_MQ_BIG_MIRROR_CRATE_4,
    },
    {
        RC_GERUDO_TRAINING_GROUND_MQ_MAZE_CRATE,
        RAND_INF_GERUDO_TRAINING_GROUND_MQ_MAZE_CRATE,
    },

    {
        RC_JABU_JABUS_BELLY_PLATFORM_ROOM_SMALL_CRATE_1,
        RAND_INF_JABU_JABUS_BELLY_PLATFORM_ROOM_SMALL_CRATE_1,
    },
    {
        RC_JABU_JABUS_BELLY_PLATFORM_ROOM_SMALL_CRATE_2,
        RAND_INF_JABU_JABUS_BELLY_PLATFORM_ROOM_SMALL_CRATE_2,
    },
    {
        RC_FIRE_TEMPLE_AFTER_HAMMER_SMALL_CRATE_1,
        RAND_INF_FIRE_TEMPLE_AFTER_HAMMER_SMALL_CRATE_1,
    },
    {
        RC_FIRE_TEMPLE_AFTER_HAMMER_SMALL_CRATE_2,
        RAND_INF_FIRE_TEMPLE_AFTER_HAMMER_SMALL_CRATE_2,
    },
    {
        RC_SPIRIT_TEMPLE_BEFORE_CHILD_CLIMB_SMALL_CRATE_1,
        RAND_INF_SPIRIT_TEMPLE_BEFORE_CHILD_CLIMB_SMALL_CRATE_1,
    },
    {
        RC_SPIRIT_TEMPLE_BEFORE_CHILD_CLIMB_SMALL_CRATE_2,
        RAND_INF_SPIRIT_TEMPLE_BEFORE_CHILD_CLIMB_SMALL_CRATE_2,
    },

    {
        RC_JABU_JABUS_BELLY_MQ_TRIPLE_HALLWAY_SMALL_CRATE_1,
        RAND_INF_JABU_JABUS_BELLY_MQ_TRIPLE_HALLWAY_SMALL_CRATE_1,
    },
    {
        RC_JABU_JABUS_BELLY_MQ_TRIPLE_HALLWAY_SMALL_CRATE_2,
        RAND_INF_JABU_JABUS_BELLY_MQ_TRIPLE_HALLWAY_SMALL_CRATE_2,
    },
    {
        RC_JABU_JABUS_BELLY_MQ_JIGGLIES_SMALL_CRATE_1,
        RAND_INF_JABU_JABUS_BELLY_MQ_JIGGLIES_SMALL_CRATE_1,
    },
    {
        RC_JABU_JABUS_BELLY_MQ_JIGGLIES_SMALL_CRATE_2,
        RAND_INF_JABU_JABUS_BELLY_MQ_JIGGLIES_SMALL_CRATE_2,
    },
    {
        RC_FOREST_TEMPLE_MQ_FROZEN_EYE_SWITCH_SMALL_CRATE_1,
        RAND_INF_FOREST_TEMPLE_MQ_FROZEN_EYE_SWITCH_SMALL_CRATE_1,
    },
    {
        RC_FOREST_TEMPLE_MQ_FROZEN_EYE_SWITCH_SMALL_CRATE_2,
        RAND_INF_FOREST_TEMPLE_MQ_FROZEN_EYE_SWITCH_SMALL_CRATE_2,
    },
    {
        RC_FOREST_TEMPLE_MQ_FROZEN_EYE_SWITCH_SMALL_CRATE_3,
        RAND_INF_FOREST_TEMPLE_MQ_FROZEN_EYE_SWITCH_SMALL_CRATE_3,
    },
    {
        RC_FIRE_TEMPLE_MQ_LIZALFOS_MAZE_UPPER_SMALL_CRATE_1,
        RAND_INF_FIRE_TEMPLE_MQ_LIZALFOS_MAZE_UPPER_SMALL_CRATE_1,
    },
    {
        RC_FIRE_TEMPLE_MQ_LIZALFOS_MAZE_UPPER_SMALL_CRATE_2,
        RAND_INF_FIRE_TEMPLE_MQ_LIZALFOS_MAZE_UPPER_SMALL_CRATE_2,
    },
    {
        RC_FIRE_TEMPLE_MQ_LAVA_TORCH_SMALL_CRATE_1,
        RAND_INF_FIRE_TEMPLE_MQ_LAVA_TORCH_SMALL_CRATE_1,
    },
    {
        RC_FIRE_TEMPLE_MQ_LAVA_TORCH_SMALL_CRATE_2,
        RAND_INF_FIRE_TEMPLE_MQ_LAVA_TORCH_SMALL_CRATE_2,
    },
    {
        RC_FIRE_TEMPLE_MQ_LAVA_TORCH_SMALL_CRATE_3,
        RAND_INF_FIRE_TEMPLE_MQ_LAVA_TORCH_SMALL_CRATE_3,
    },
    {
        RC_FIRE_TEMPLE_MQ_LAVA_TORCH_SMALL_CRATE_4,
        RAND_INF_FIRE_TEMPLE_MQ_LAVA_TORCH_SMALL_CRATE_4,
    },
    {
        RC_FIRE_TEMPLE_MQ_LAVA_TORCH_SMALL_CRATE_5,
        RAND_INF_FIRE_TEMPLE_MQ_LAVA_TORCH_SMALL_CRATE_5,
    },
    {
        RC_WATER_TEMPLE_MQ_DRAGON_ROOM_TORCHES_SMALL_CRATE_1,
        RAND_INF_WATER_TEMPLE_MQ_DRAGON_ROOM_TORCHES_SMALL_CRATE_1,
    },
    {
        RC_WATER_TEMPLE_MQ_DRAGON_ROOM_TORCHES_SMALL_CRATE_2,
        RAND_INF_WATER_TEMPLE_MQ_DRAGON_ROOM_TORCHES_SMALL_CRATE_2,
    },
    {
        RC_WATER_TEMPLE_MQ_DRAGON_ROOM_TORCHES_SMALL_CRATE_3,
        RAND_INF_WATER_TEMPLE_MQ_DRAGON_ROOM_TORCHES_SMALL_CRATE_3,
    },
    {
        RC_WATER_TEMPLE_MQ_STORAGE_ROOM_A_SMALL_CRATE_1,
        RAND_INF_WATER_TEMPLE_MQ_STORAGE_ROOM_A_SMALL_CRATE_1,
    },
    {
        RC_WATER_TEMPLE_MQ_STORAGE_ROOM_A_SMALL_CRATE_2,
        RAND_INF_WATER_TEMPLE_MQ_STORAGE_ROOM_A_SMALL_CRATE_2,
    },
    {
        RC_WATER_TEMPLE_MQ_STORAGE_ROOM_A_SMALL_CRATE_3,
        RAND_INF_WATER_TEMPLE_MQ_STORAGE_ROOM_A_SMALL_CRATE_3,
    },
    {
        RC_WATER_TEMPLE_MQ_STORAGE_ROOM_A_SMALL_CRATE_4,
        RAND_INF_WATER_TEMPLE_MQ_STORAGE_ROOM_A_SMALL_CRATE_4,
    },
    {
        RC_WATER_TEMPLE_MQ_GS_STORAGE_ROOM_LOWER_SMALL_CRATE,
        RAND_INF_WATER_TEMPLE_MQ_GS_STORAGE_ROOM_LOWER_SMALL_CRATE,
    },
    {
        RC_WATER_TEMPLE_MQ_GS_STORAGE_ROOM_UPPER_SMALL_CRATE,
        RAND_INF_WATER_TEMPLE_MQ_GS_STORAGE_ROOM_UPPER_SMALL_CRATE,
    },
    {
        RC_SHADOW_TEMPLE_MQ_TRUTH_SPINNER_SMALL_CRATE_1,
        RAND_INF_SHADOW_TEMPLE_MQ_TRUTH_SPINNER_SMALL_CRATE_1,
    },
    {
        RC_SHADOW_TEMPLE_MQ_TRUTH_SPINNER_SMALL_CRATE_2,
        RAND_INF_SHADOW_TEMPLE_MQ_TRUTH_SPINNER_SMALL_CRATE_2,
    },
    {
        RC_SHADOW_TEMPLE_MQ_TRUTH_SPINNER_SMALL_CRATE_3,
        RAND_INF_SHADOW_TEMPLE_MQ_TRUTH_SPINNER_SMALL_CRATE_3,
    },
    {
        RC_SHADOW_TEMPLE_MQ_TRUTH_SPINNER_SMALL_CRATE_4,
        RAND_INF_SHADOW_TEMPLE_MQ_TRUTH_SPINNER_SMALL_CRATE_4,
    },
    {
        RC_SPIRIT_TEMPLE_MQ_STATUE_SMALL_CRATE,
        RAND_INF_SPIRIT_TEMPLE_MQ_STATUE_SMALL_CRATE,
    },
    {
        RC_SPIRIT_TEMPLE_MQ_BEAMOS_SMALL_CRATE,
        RAND_INF_SPIRIT_TEMPLE_MQ_BEAMOS_SMALL_CRATE,
    },
    { RC_KF_CIRCLE_ROCK_1, RAND_INF_KF_CIRCLE_ROCK_1 },
    { RC_KF_CIRCLE_ROCK_2, RAND_INF_KF_CIRCLE_ROCK_2 },
    { RC_KF_CIRCLE_ROCK_3, RAND_INF_KF_CIRCLE_ROCK_3 },
    { RC_KF_CIRCLE_ROCK_4, RAND_INF_KF_CIRCLE_ROCK_4 },
    { RC_KF_CIRCLE_ROCK_5, RAND_INF_KF_CIRCLE_ROCK_5 },
    { RC_KF_CIRCLE_ROCK_6, RAND_INF_KF_CIRCLE_ROCK_6 },
    { RC_KF_CIRCLE_ROCK_7, RAND_INF_KF_CIRCLE_ROCK_7 },
    { RC_KF_CIRCLE_ROCK_8, RAND_INF_KF_CIRCLE_ROCK_8 },
    { RC_KF_ROCK_BY_SARIAS_HOUSE, RAND_INF_KF_ROCK_BY_SARIAS_HOUSE },
    { RC_KF_ROCK_BEHIND_SARIAS_HOUSE, RAND_INF_KF_ROCK_BEHIND_SARIAS_HOUSE },
    { RC_KF_ROCK_BY_MIDOS_HOUSE, RAND_INF_KF_ROCK_BY_MIDOS_HOUSE },
    { RC_KF_ROCK_BY_KNOW_IT_ALLS_HOUSE, RAND_INF_KF_ROCK_BY_KNOW_IT_ALLS_HOUSE },
    { RC_LW_BOULDER_BY_GORON_CITY, RAND_INF_LW_BOULDER_BY_GORON_CITY },
    { RC_LW_BOULDER_BY_SACRED_FOREST_MEADOW, RAND_INF_LW_BOULDER_BY_SACRED_FOREST_MEADOW },
    { RC_LW_RUPEE_BOULDER, RAND_INF_LW_RUPEE_BOULDER },
    { RC_HC_ROCK_1, RAND_INF_HC_ROCK_1 },
    { RC_HC_ROCK_2, RAND_INF_HC_ROCK_2 },
    { RC_HC_ROCK_3, RAND_INF_HC_ROCK_3 },
    { RC_HC_BOULDER, RAND_INF_HC_BOULDER },
    { RC_OGC_BRONZE_BOULDER_1, RAND_INF_OGC_BRONZE_BOULDER_1 },
    { RC_OGC_BRONZE_BOULDER_2, RAND_INF_OGC_BRONZE_BOULDER_2 },
    { RC_OGC_BRONZE_BOULDER_3, RAND_INF_OGC_BRONZE_BOULDER_3 },
    { RC_OGC_SILVER_BOULDER_1, RAND_INF_OGC_SILVER_BOULDER_1 },
    { RC_OGC_SILVER_BOULDER_2, RAND_INF_OGC_SILVER_BOULDER_2 },
    { RC_OGC_SILVER_BOULDER_3, RAND_INF_OGC_SILVER_BOULDER_3 },
    { RC_OGC_SILVER_BOULDER_4, RAND_INF_OGC_SILVER_BOULDER_4 },
    { RC_DMC_CIRCLE_ROCK_1, RAND_INF_DMC_CIRCLE_ROCK_1 },
    { RC_DMC_CIRCLE_ROCK_2, RAND_INF_DMC_CIRCLE_ROCK_2 },
    { RC_DMC_CIRCLE_ROCK_3, RAND_INF_DMC_CIRCLE_ROCK_3 },
    { RC_DMC_CIRCLE_ROCK_4, RAND_INF_DMC_CIRCLE_ROCK_4 },
    { RC_DMC_CIRCLE_ROCK_5, RAND_INF_DMC_CIRCLE_ROCK_5 },
    { RC_DMC_CIRCLE_ROCK_6, RAND_INF_DMC_CIRCLE_ROCK_6 },
    { RC_DMC_CIRCLE_ROCK_7, RAND_INF_DMC_CIRCLE_ROCK_7 },
    { RC_DMC_CIRCLE_ROCK_8, RAND_INF_DMC_CIRCLE_ROCK_8 },
    { RC_DMC_ROCK_BY_FIRE_TEMPLE_1, RAND_INF_DMC_ROCK_BY_FIRE_TEMPLE_1 },
    { RC_DMC_ROCK_BY_FIRE_TEMPLE_2, RAND_INF_DMC_ROCK_BY_FIRE_TEMPLE_2 },
    { RC_DMC_ROCK_BY_FIRE_TEMPLE_3, RAND_INF_DMC_ROCK_BY_FIRE_TEMPLE_3 },
    { RC_DMC_ROCK_BY_FIRE_TEMPLE_4, RAND_INF_DMC_ROCK_BY_FIRE_TEMPLE_4 },
    { RC_DMC_ROCK_BY_FIRE_TEMPLE_5, RAND_INF_DMC_ROCK_BY_FIRE_TEMPLE_5 },
    { RC_DMC_GOSSIP_ROCK_1, RAND_INF_DMC_GOSSIP_ROCK_1 },
    { RC_DMC_GOSSIP_ROCK_2, RAND_INF_DMC_GOSSIP_ROCK_2 },
    { RC_DMC_BOULDER_1, RAND_INF_DMC_BOULDER_1 },
    { RC_DMC_BOULDER_2, RAND_INF_DMC_BOULDER_2 },
    { RC_DMC_BOULDER_3, RAND_INF_DMC_BOULDER_3 },
    { RC_DMC_BRONZE_BOULDER_1, RAND_INF_DMC_BRONZE_BOULDER_1 },
    { RC_DMC_BRONZE_BOULDER_2, RAND_INF_DMC_BRONZE_BOULDER_2 },
    { RC_DMC_BRONZE_BOULDER_3, RAND_INF_DMC_BRONZE_BOULDER_3 },
    { RC_DMC_BRONZE_BOULDER_SHORTCUT, RAND_INF_DMC_BRONZE_BOULDER_SHORTCUT },
    { RC_GV_SILVER_BOULDER, RAND_INF_GV_SILVER_BOULDER },
    { RC_GV_ROCK_1, RAND_INF_GV_ROCK_1 },
    { RC_GV_ROCK_2, RAND_INF_GV_ROCK_2 },
    { RC_GV_ROCK_3, RAND_INF_GV_ROCK_3 },
    { RC_GV_UNDERWATER_ROCK_1, RAND_INF_GV_UNDERWATER_ROCK_1 },
    { RC_GV_UNDERWATER_ROCK_2, RAND_INF_GV_UNDERWATER_ROCK_2 },
    { RC_GV_UNDERWATER_ROCK_3, RAND_INF_GV_UNDERWATER_ROCK_3 },
    { RC_GV_ROCK_ACROSS_BRIDGE_1, RAND_INF_GV_ROCK_ACROSS_BRIDGE_1 },
    { RC_GV_ROCK_ACROSS_BRIDGE_2, RAND_INF_GV_ROCK_ACROSS_BRIDGE_2 },
    { RC_GV_ROCK_ACROSS_BRIDGE_3, RAND_INF_GV_ROCK_ACROSS_BRIDGE_3 },
    { RC_GV_ROCK_ACROSS_BRIDGE_4, RAND_INF_GV_ROCK_ACROSS_BRIDGE_4 },
    { RC_GV_BOULDER_1, RAND_INF_GV_BOULDER_1 },
    { RC_GV_BOULDER_2, RAND_INF_GV_BOULDER_2 },
    { RC_GV_BOULDER_ACROSS_BRIDGE, RAND_INF_GV_BOULDER_ACROSS_BRIDGE },
    { RC_GV_BRONZE_BOULDER_1, RAND_INF_GV_BRONZE_BOULDER_1 },
    { RC_GV_BRONZE_BOULDER_2, RAND_INF_GV_BRONZE_BOULDER_2 },
    { RC_GV_BRONZE_BOULDER_ACROSS_BRIDGE_1, RAND_INF_GV_BRONZE_BOULDER_ACROSS_BRIDGE_1 },
    { RC_GV_BRONZE_BOULDER_ACROSS_BRIDGE_2, RAND_INF_GV_BRONZE_BOULDER_ACROSS_BRIDGE_2 },
    { RC_GV_BRONZE_BOULDER_ACROSS_BRIDGE_3, RAND_INF_GV_BRONZE_BOULDER_ACROSS_BRIDGE_3 },
    { RC_GV_BRONZE_BOULDER_ACROSS_BRIDGE_4, RAND_INF_GV_BRONZE_BOULDER_ACROSS_BRIDGE_4 },
    { RC_GV_BRONZE_BOULDER_ACROSS_BRIDGE_5, RAND_INF_GV_BRONZE_BOULDER_ACROSS_BRIDGE_5 },
    { RC_GV_BRONZE_BOULDER_ACROSS_BRIDGE_6, RAND_INF_GV_BRONZE_BOULDER_ACROSS_BRIDGE_6 },
    { RC_HF_SILVER_BOULDER, RAND_INF_HF_SILVER_BOULDER },
    { RC_HF_ROCK_1, RAND_INF_HF_ROCK_1 },
    { RC_HF_ROCK_2, RAND_INF_HF_ROCK_2 },
    { RC_HF_ROCK_3, RAND_INF_HF_ROCK_3 },
    { RC_HF_ROCK_4, RAND_INF_HF_ROCK_4 },
    { RC_HF_ROCK_5, RAND_INF_HF_ROCK_5 },
    { RC_HF_ROCK_6, RAND_INF_HF_ROCK_6 },
    { RC_HF_ROCK_7, RAND_INF_HF_ROCK_7 },
    { RC_HF_ROCK_8, RAND_INF_HF_ROCK_8 },
    { RC_HF_BOULDER_NORTH, RAND_INF_HF_BOULDER_NORTH },
    { RC_HF_BOULDER_BY_MARKET, RAND_INF_HF_BOULDER_BY_MARKET },
    { RC_HF_BOULDER_SOUTH, RAND_INF_HF_BOULDER_SOUTH },
    { RC_HF_BRONZE_BOULDER_1, RAND_INF_HF_BRONZE_BOULDER_1 },
    { RC_HF_BRONZE_BOULDER_2, RAND_INF_HF_BRONZE_BOULDER_2 },
    { RC_HF_BRONZE_BOULDER_3, RAND_INF_HF_BRONZE_BOULDER_3 },
    { RC_HF_BRONZE_BOULDER_4, RAND_INF_HF_BRONZE_BOULDER_4 },
    { RC_KAK_SILVER_BOULDER, RAND_INF_KAK_SILVER_BOULDER },
    { RC_KAK_ROCK_1, RAND_INF_KAK_ROCK_1 },
    { RC_KAK_ROCK_2, RAND_INF_KAK_ROCK_2 },
    { RC_GY_ROCK, RAND_INF_GY_ROCK },
    { RC_LH_ROCK, RAND_INF_LH_ROCK },
    { RC_ZD_CIRCLE_ROCK_1, RAND_INF_ZD_CIRCLE_ROCK_1 },
    { RC_ZD_CIRCLE_ROCK_2, RAND_INF_ZD_CIRCLE_ROCK_2 },
    { RC_ZD_CIRCLE_ROCK_3, RAND_INF_ZD_CIRCLE_ROCK_3 },
    { RC_ZD_CIRCLE_ROCK_4, RAND_INF_ZD_CIRCLE_ROCK_4 },
    { RC_ZD_CIRCLE_ROCK_5, RAND_INF_ZD_CIRCLE_ROCK_5 },
    { RC_ZD_CIRCLE_ROCK_6, RAND_INF_ZD_CIRCLE_ROCK_6 },
    { RC_ZD_CIRCLE_ROCK_7, RAND_INF_ZD_CIRCLE_ROCK_7 },
    { RC_ZD_CIRCLE_ROCK_8, RAND_INF_ZD_CIRCLE_ROCK_8 },
    { RC_ZF_BOULDER, RAND_INF_ZF_BOULDER },
    { RC_ZF_SILVER_BOULDER, RAND_INF_ZF_SILVER_BOULDER },
    { RC_ZF_UNDERGROUND_BOULDER, RAND_INF_ZF_UNDERGROUND_BOULDER },
    { RC_ZR_BOULDER_1, RAND_INF_ZR_BOULDER_1 },
    { RC_ZR_BOULDER_2, RAND_INF_ZR_BOULDER_2 },
    { RC_ZR_BOULDER_3, RAND_INF_ZR_BOULDER_3 },
    { RC_ZR_BOULDER_4, RAND_INF_ZR_BOULDER_4 },
    { RC_ZR_CIRCLE_ROCK_1, RAND_INF_ZR_CIRCLE_ROCK_1 },
    { RC_ZR_CIRCLE_ROCK_2, RAND_INF_ZR_CIRCLE_ROCK_2 },
    { RC_ZR_CIRCLE_ROCK_3, RAND_INF_ZR_CIRCLE_ROCK_3 },
    { RC_ZR_CIRCLE_ROCK_4, RAND_INF_ZR_CIRCLE_ROCK_4 },
    { RC_ZR_CIRCLE_ROCK_5, RAND_INF_ZR_CIRCLE_ROCK_5 },
    { RC_ZR_CIRCLE_ROCK_6, RAND_INF_ZR_CIRCLE_ROCK_6 },
    { RC_ZR_CIRCLE_ROCK_7, RAND_INF_ZR_CIRCLE_ROCK_7 },
    { RC_ZR_CIRCLE_ROCK_8, RAND_INF_ZR_CIRCLE_ROCK_8 },
    { RC_ZR_UPPER_CIRCLE_BOULDER, RAND_INF_ZR_UPPER_CIRCLE_BOULDER },
    { RC_ZR_UPPER_CIRCLE_ROCK_1, RAND_INF_ZR_UPPER_CIRCLE_ROCK_1 },
    { RC_ZR_UPPER_CIRCLE_ROCK_2, RAND_INF_ZR_UPPER_CIRCLE_ROCK_2 },
    { RC_ZR_UPPER_CIRCLE_ROCK_3, RAND_INF_ZR_UPPER_CIRCLE_ROCK_3 },
    { RC_ZR_UPPER_CIRCLE_ROCK_4, RAND_INF_ZR_UPPER_CIRCLE_ROCK_4 },
    { RC_ZR_UPPER_CIRCLE_ROCK_5, RAND_INF_ZR_UPPER_CIRCLE_ROCK_5 },
    { RC_ZR_UPPER_CIRCLE_ROCK_6, RAND_INF_ZR_UPPER_CIRCLE_ROCK_6 },
    { RC_ZR_UPPER_CIRCLE_ROCK_7, RAND_INF_ZR_UPPER_CIRCLE_ROCK_7 },
    { RC_ZR_UPPER_CIRCLE_ROCK_8, RAND_INF_ZR_UPPER_CIRCLE_ROCK_8 },
    { RC_ZR_ROCK, RAND_INF_ZR_ROCK },
    { RC_ZR_UNDERWATER_ROCK_1, RAND_INF_ZR_UNDERWATER_ROCK_1 },
    { RC_ZR_UNDERWATER_ROCK_2, RAND_INF_ZR_UNDERWATER_ROCK_2 },
    { RC_ZR_UNDERWATER_ROCK_3, RAND_INF_ZR_UNDERWATER_ROCK_3 },
    { RC_ZR_UNDERWATER_ROCK_4, RAND_INF_ZR_UNDERWATER_ROCK_4 },
    { RC_DMT_ROCK_1, RAND_INF_DMT_ROCK_1 },
    { RC_DMT_ROCK_2, RAND_INF_DMT_ROCK_2 },
    { RC_DMT_ROCK_3, RAND_INF_DMT_ROCK_3 },
    { RC_DMT_ROCK_4, RAND_INF_DMT_ROCK_4 },
    { RC_DMT_ROCK_5, RAND_INF_DMT_ROCK_5 },
    { RC_DMT_SUMMIT_ROCK, RAND_INF_DMT_SUMMIT_ROCK },
    { RC_DMT_CIRCLE_ROCK_1, RAND_INF_DMT_CIRCLE_ROCK_1 },
    { RC_DMT_CIRCLE_ROCK_2, RAND_INF_DMT_CIRCLE_ROCK_2 },
    { RC_DMT_CIRCLE_ROCK_3, RAND_INF_DMT_CIRCLE_ROCK_3 },
    { RC_DMT_CIRCLE_ROCK_4, RAND_INF_DMT_CIRCLE_ROCK_4 },
    { RC_DMT_CIRCLE_ROCK_5, RAND_INF_DMT_CIRCLE_ROCK_5 },
    { RC_DMT_CIRCLE_ROCK_6, RAND_INF_DMT_CIRCLE_ROCK_6 },
    { RC_DMT_CIRCLE_ROCK_7, RAND_INF_DMT_CIRCLE_ROCK_7 },
    { RC_DMT_CIRCLE_ROCK_8, RAND_INF_DMT_CIRCLE_ROCK_8 },
    { RC_DMT_CHILD_BOULDER, RAND_INF_DMT_CHILD_BOULDER },
    { RC_DMT_BOULDER_1, RAND_INF_DMT_BOULDER_1 },
    { RC_DMT_BOULDER_2, RAND_INF_DMT_BOULDER_2 },
    { RC_DMT_COW_BOULDER, RAND_INF_DMT_COW_BOULDER },
    { RC_DMT_BRONZE_BOULDER_1, RAND_INF_DMT_BRONZE_BOULDER_1 },
    { RC_DMT_BRONZE_BOULDER_2, RAND_INF_DMT_BRONZE_BOULDER_2 },
    { RC_DMT_BRONZE_BOULDER_3, RAND_INF_DMT_BRONZE_BOULDER_3 },
    { RC_DMT_BRONZE_BOULDER_4, RAND_INF_DMT_BRONZE_BOULDER_4 },
    { RC_DMT_BRONZE_BOULDER_5, RAND_INF_DMT_BRONZE_BOULDER_5 },
    { RC_DMT_BRONZE_BOULDER_6, RAND_INF_DMT_BRONZE_BOULDER_6 },
    { RC_DMT_BRONZE_BOULDER_7, RAND_INF_DMT_BRONZE_BOULDER_7 },
    { RC_DMT_BRONZE_BOULDER_8, RAND_INF_DMT_BRONZE_BOULDER_8 },
    { RC_DMT_BRONZE_BOULDER_9, RAND_INF_DMT_BRONZE_BOULDER_9 },
    { RC_DMT_BRONZE_BOULDER_10, RAND_INF_DMT_BRONZE_BOULDER_10 },
    { RC_DMT_BRONZE_BOULDER_11, RAND_INF_DMT_BRONZE_BOULDER_11 },
    { RC_GC_LW_BOULDER_1, RAND_INF_GC_LW_BOULDER_1 },
    { RC_GC_LW_BOULDER_2, RAND_INF_GC_LW_BOULDER_2 },
    { RC_GC_LW_BOULDER_3, RAND_INF_GC_LW_BOULDER_3 },
    { RC_GC_ENTRANCE_BOULDER_1, RAND_INF_GC_ENTRANCE_BOULDER_1 },
    { RC_GC_ENTRANCE_BOULDER_2, RAND_INF_GC_ENTRANCE_BOULDER_2 },
    { RC_GC_ENTRANCE_BOULDER_3, RAND_INF_GC_ENTRANCE_BOULDER_3 },
    { RC_GC_MAZE_SILVER_BOULDER_1, RAND_INF_GC_MAZE_SILVER_BOULDER_1 },
    { RC_GC_MAZE_SILVER_BOULDER_2, RAND_INF_GC_MAZE_SILVER_BOULDER_2 },
    { RC_GC_MAZE_SILVER_BOULDER_3, RAND_INF_GC_MAZE_SILVER_BOULDER_3 },
    { RC_GC_MAZE_SILVER_BOULDER_4, RAND_INF_GC_MAZE_SILVER_BOULDER_4 },
    { RC_GC_MAZE_SILVER_BOULDER_5, RAND_INF_GC_MAZE_SILVER_BOULDER_5 },
    { RC_GC_MAZE_SILVER_BOULDER_6, RAND_INF_GC_MAZE_SILVER_BOULDER_6 },
    { RC_GC_MAZE_SILVER_BOULDER_7, RAND_INF_GC_MAZE_SILVER_BOULDER_7 },
    { RC_GC_MAZE_SILVER_BOULDER_8, RAND_INF_GC_MAZE_SILVER_BOULDER_8 },
    { RC_GC_MAZE_SILVER_BOULDER_9, RAND_INF_GC_MAZE_SILVER_BOULDER_9 },
    { RC_GC_MAZE_SILVER_BOULDER_10, RAND_INF_GC_MAZE_SILVER_BOULDER_10 },
    { RC_GC_MAZE_SILVER_BOULDER_11, RAND_INF_GC_MAZE_SILVER_BOULDER_11 },
    { RC_GC_MAZE_SILVER_BOULDER_12, RAND_INF_GC_MAZE_SILVER_BOULDER_12 },
    { RC_GC_MAZE_SILVER_BOULDER_13, RAND_INF_GC_MAZE_SILVER_BOULDER_13 },
    { RC_GC_MAZE_SILVER_BOULDER_14, RAND_INF_GC_MAZE_SILVER_BOULDER_14 },
    { RC_GC_MAZE_SILVER_BOULDER_15, RAND_INF_GC_MAZE_SILVER_BOULDER_15 },
    { RC_GC_MAZE_SILVER_BOULDER_16, RAND_INF_GC_MAZE_SILVER_BOULDER_16 },
    { RC_GC_MAZE_SILVER_BOULDER_17, RAND_INF_GC_MAZE_SILVER_BOULDER_17 },
    { RC_GC_MAZE_SILVER_BOULDER_18, RAND_INF_GC_MAZE_SILVER_BOULDER_18 },
    { RC_GC_MAZE_SILVER_BOULDER_19, RAND_INF_GC_MAZE_SILVER_BOULDER_19 },
    { RC_GC_MAZE_SILVER_BOULDER_20, RAND_INF_GC_MAZE_SILVER_BOULDER_20 },
    { RC_GC_MAZE_SILVER_BOULDER_21, RAND_INF_GC_MAZE_SILVER_BOULDER_21 },
    { RC_GC_MAZE_SILVER_BOULDER_22, RAND_INF_GC_MAZE_SILVER_BOULDER_22 },
    { RC_GC_MAZE_SILVER_BOULDER_23, RAND_INF_GC_MAZE_SILVER_BOULDER_23 },
    { RC_GC_MAZE_SILVER_BOULDER_24, RAND_INF_GC_MAZE_SILVER_BOULDER_24 },
    { RC_GC_MAZE_SILVER_BOULDER_25, RAND_INF_GC_MAZE_SILVER_BOULDER_25 },
    { RC_GC_MAZE_SILVER_BOULDER_26, RAND_INF_GC_MAZE_SILVER_BOULDER_26 },
    { RC_GC_MAZE_SILVER_BOULDER_27, RAND_INF_GC_MAZE_SILVER_BOULDER_27 },
    { RC_GC_MAZE_SILVER_BOULDER_28, RAND_INF_GC_MAZE_SILVER_BOULDER_28 },
    { RC_GC_MAZE_SILVER_BOULDER_29, RAND_INF_GC_MAZE_SILVER_BOULDER_29 },
    { RC_GC_MAZE_BOULDER_1, RAND_INF_GC_MAZE_BOULDER_1 },
    { RC_GC_MAZE_BOULDER_2, RAND_INF_GC_MAZE_BOULDER_2 },
    { RC_GC_MAZE_BOULDER_3, RAND_INF_GC_MAZE_BOULDER_3 },
    { RC_GC_MAZE_BOULDER_4, RAND_INF_GC_MAZE_BOULDER_4 },
    { RC_GC_MAZE_BOULDER_5, RAND_INF_GC_MAZE_BOULDER_5 },
    { RC_GC_MAZE_BOULDER_6, RAND_INF_GC_MAZE_BOULDER_6 },
    { RC_GC_MAZE_BOULDER_7, RAND_INF_GC_MAZE_BOULDER_7 },
    { RC_GC_MAZE_BOULDER_8, RAND_INF_GC_MAZE_BOULDER_8 },
    { RC_GC_MAZE_BOULDER_9, RAND_INF_GC_MAZE_BOULDER_9 },
    { RC_GC_MAZE_BOULDER_10, RAND_INF_GC_MAZE_BOULDER_10 },
    { RC_GC_MAZE_BRONZE_BOULDER_1, RAND_INF_GC_MAZE_BRONZE_BOULDER_1 },
    { RC_GC_MAZE_BRONZE_BOULDER_2, RAND_INF_GC_MAZE_BRONZE_BOULDER_2 },
    { RC_GC_MAZE_BRONZE_BOULDER_3, RAND_INF_GC_MAZE_BRONZE_BOULDER_3 },
    { RC_GC_MAZE_BRONZE_BOULDER_4, RAND_INF_GC_MAZE_BRONZE_BOULDER_4 },
    { RC_GC_MAZE_BRONZE_BOULDER_5, RAND_INF_GC_MAZE_BRONZE_BOULDER_5 },
    { RC_GC_MAZE_ROCK, RAND_INF_GC_MAZE_ROCK },
    { RC_COLOSSUS_SILVER_BOULDER, RAND_INF_COLOSSUS_SILVER_BOULDER },
    { RC_COLOSSUS_ROCK, RAND_INF_COLOSSUS_ROCK },
    { RC_COLOSSUS_CIRCLE_1_ROCK_1, RAND_INF_COLOSSUS_CIRCLE_1_ROCK_1 },
    { RC_COLOSSUS_CIRCLE_1_ROCK_2, RAND_INF_COLOSSUS_CIRCLE_1_ROCK_2 },
    { RC_COLOSSUS_CIRCLE_1_ROCK_3, RAND_INF_COLOSSUS_CIRCLE_1_ROCK_3 },
    { RC_COLOSSUS_CIRCLE_1_ROCK_4, RAND_INF_COLOSSUS_CIRCLE_1_ROCK_4 },
    { RC_COLOSSUS_CIRCLE_1_ROCK_5, RAND_INF_COLOSSUS_CIRCLE_1_ROCK_5 },
    { RC_COLOSSUS_CIRCLE_1_ROCK_6, RAND_INF_COLOSSUS_CIRCLE_1_ROCK_6 },
    { RC_COLOSSUS_CIRCLE_1_ROCK_7, RAND_INF_COLOSSUS_CIRCLE_1_ROCK_7 },
    { RC_COLOSSUS_CIRCLE_1_ROCK_8, RAND_INF_COLOSSUS_CIRCLE_1_ROCK_8 },
    { RC_COLOSSUS_CIRCLE_2_ROCK_1, RAND_INF_COLOSSUS_CIRCLE_2_ROCK_1 },
    { RC_COLOSSUS_CIRCLE_2_ROCK_2, RAND_INF_COLOSSUS_CIRCLE_2_ROCK_2 },
    { RC_COLOSSUS_CIRCLE_2_ROCK_3, RAND_INF_COLOSSUS_CIRCLE_2_ROCK_3 },
    { RC_COLOSSUS_CIRCLE_2_ROCK_4, RAND_INF_COLOSSUS_CIRCLE_2_ROCK_4 },
    { RC_COLOSSUS_CIRCLE_2_ROCK_5, RAND_INF_COLOSSUS_CIRCLE_2_ROCK_5 },
    { RC_COLOSSUS_CIRCLE_2_ROCK_6, RAND_INF_COLOSSUS_CIRCLE_2_ROCK_6 },
    { RC_COLOSSUS_CIRCLE_2_ROCK_7, RAND_INF_COLOSSUS_CIRCLE_2_ROCK_7 },
    { RC_COLOSSUS_CIRCLE_2_ROCK_8, RAND_INF_COLOSSUS_CIRCLE_2_ROCK_8 },
    { RC_HC_STORMS_GROTTO_ROCK_1, RAND_INF_HC_STORMS_GROTTO_ROCK_1 },
    { RC_HC_STORMS_GROTTO_ROCK_2, RAND_INF_HC_STORMS_GROTTO_ROCK_2 },
    { RC_HC_STORMS_GROTTO_ROCK_3, RAND_INF_HC_STORMS_GROTTO_ROCK_3 },
    { RC_HC_STORMS_GROTTO_ROCK_4, RAND_INF_HC_STORMS_GROTTO_ROCK_4 },
    { RC_HC_STORMS_GROTTO_ROCK_5, RAND_INF_HC_STORMS_GROTTO_ROCK_5 },
    { RC_HC_STORMS_GROTTO_ROCK_6, RAND_INF_HC_STORMS_GROTTO_ROCK_6 },
    { RC_HC_STORMS_GROTTO_ROCK_7, RAND_INF_HC_STORMS_GROTTO_ROCK_7 },
    { RC_HC_STORMS_GROTTO_ROCK_8, RAND_INF_HC_STORMS_GROTTO_ROCK_8 },
    { RC_BOTW_BOULDER_1, RAND_INF_BOTW_BOULDER_1 },
    { RC_BOTW_BOULDER_2, RAND_INF_BOTW_BOULDER_2 },
    { RC_BOTW_BOULDER_3, RAND_INF_BOTW_BOULDER_3 },
    { RC_BOTW_BOULDER_4, RAND_INF_BOTW_BOULDER_4 },
    { RC_BOTW_BOULDER_5, RAND_INF_BOTW_BOULDER_5 },
    { RC_BOTW_BOULDER_6, RAND_INF_BOTW_BOULDER_6 },
    { RC_DEKU_TREE_MQ_BOULDER_1, RAND_INF_DEKU_TREE_MQ_BOULDER_1 },
    { RC_DEKU_TREE_MQ_BOULDER_2, RAND_INF_DEKU_TREE_MQ_BOULDER_2 },
    { RC_DEKU_TREE_MQ_BOULDER_3, RAND_INF_DEKU_TREE_MQ_BOULDER_3 },
    { RC_DODONGOS_CAVERN_MQ_LOBBY_BOULDER_1, RAND_INF_DODONGOS_CAVERN_MQ_LOBBY_BOULDER_1 },
    { RC_DODONGOS_CAVERN_MQ_LOBBY_BOULDER_2, RAND_INF_DODONGOS_CAVERN_MQ_LOBBY_BOULDER_2 },
    { RC_DODONGOS_CAVERN_MQ_MOUTH_SIDE_BRIDGE_BOULDER_1, RAND_INF_DODONGOS_CAVERN_MQ_MOUTH_SIDE_BRIDGE_BOULDER_1 },
    { RC_DODONGOS_CAVERN_MQ_MOUTH_SIDE_BRIDGE_BOULDER_2, RAND_INF_DODONGOS_CAVERN_MQ_MOUTH_SIDE_BRIDGE_BOULDER_2 },
    { RC_DODONGOS_CAVERN_MQ_MOUTH_SIDE_BRIDGE_BOULDER_3, RAND_INF_DODONGOS_CAVERN_MQ_MOUTH_SIDE_BRIDGE_BOULDER_3 },
    { RC_DODONGOS_CAVERN_MQ_RIGHT_SIDE_BOULDER_1, RAND_INF_DODONGOS_CAVERN_MQ_RIGHT_SIDE_BOULDER_1 },
    { RC_DODONGOS_CAVERN_MQ_RIGHT_SIDE_BOULDER_2, RAND_INF_DODONGOS_CAVERN_MQ_RIGHT_SIDE_BOULDER_2 },
    { RC_DODONGOS_CAVERN_MQ_LIZALFOS_ROOM_BOULDER_1, RAND_INF_DODONGOS_CAVERN_MQ_LIZALFOS_ROOM_BOULDER_1 },
    { RC_DODONGOS_CAVERN_MQ_LIZALFOS_ROOM_BOULDER_2, RAND_INF_DODONGOS_CAVERN_MQ_LIZALFOS_ROOM_BOULDER_2 },
    { RC_DODONGOS_CAVERN_MQ_LIZALFOS_ROOM_BOULDER_3, RAND_INF_DODONGOS_CAVERN_MQ_LIZALFOS_ROOM_BOULDER_3 },
    { RC_DODONGOS_CAVERN_MQ_LIZALFOS_ROOM_BOULDER_4, RAND_INF_DODONGOS_CAVERN_MQ_LIZALFOS_ROOM_BOULDER_4 },
    { RC_DODONGOS_CAVERN_MQ_LIZALFOS_ROOM_BOULDER_5, RAND_INF_DODONGOS_CAVERN_MQ_LIZALFOS_ROOM_BOULDER_5 },
    { RC_DODONGOS_CAVERN_MQ_LIZALFOS_ROOM_BOULDER_6, RAND_INF_DODONGOS_CAVERN_MQ_LIZALFOS_ROOM_BOULDER_6 },
    { RC_DODONGOS_CAVERN_MQ_LIZALFOS_ROOM_BOULDER_7, RAND_INF_DODONGOS_CAVERN_MQ_LIZALFOS_ROOM_BOULDER_7 },
    { RC_DODONGOS_CAVERN_MQ_LIZALFOS_ROOM_BOULDER_8, RAND_INF_DODONGOS_CAVERN_MQ_LIZALFOS_ROOM_BOULDER_8 },
    { RC_DODONGOS_CAVERN_MQ_LIZALFOS_ROOM_BOULDER_9, RAND_INF_DODONGOS_CAVERN_MQ_LIZALFOS_ROOM_BOULDER_9 },
    { RC_DODONGOS_CAVERN_MQ_LIZALFOS_ROOM_BOULDER_10, RAND_INF_DODONGOS_CAVERN_MQ_LIZALFOS_ROOM_BOULDER_10 },
    { RC_DODONGOS_CAVERN_MQ_LIZALFOS_ROOM_BOULDER_11, RAND_INF_DODONGOS_CAVERN_MQ_LIZALFOS_ROOM_BOULDER_11 },
    { RC_DODONGOS_CAVERN_MQ_LIZALFOS_ROOM_BOULDER_12, RAND_INF_DODONGOS_CAVERN_MQ_LIZALFOS_ROOM_BOULDER_12 },
    { RC_DODONGOS_CAVERN_MQ_TWO_FLAMES_BOULDER, RAND_INF_DODONGOS_CAVERN_MQ_TWO_FLAMES_BOULDER },
    { RC_JABU_JABUS_BELLY_MQ_ENTRANCE_BOULDER, RAND_INF_JABU_JABUS_BELLY_MQ_ENTRANCE_BOULDER },
    { RC_JABU_JABUS_BELLY_MQ_HOLES_ROOM_BOULDER_1, RAND_INF_JABU_JABUS_BELLY_MQ_HOLES_ROOM_BOULDER_1 },
    { RC_JABU_JABUS_BELLY_MQ_HOLES_ROOM_BOULDER_2, RAND_INF_JABU_JABUS_BELLY_MQ_HOLES_ROOM_BOULDER_2 },
    { RC_JABU_JABUS_BELLY_MQ_HOLES_ROOM_WALL_BOULDER_1, RAND_INF_JABU_JABUS_BELLY_MQ_HOLES_ROOM_WALL_BOULDER_1 },
    { RC_JABU_JABUS_BELLY_MQ_HOLES_ROOM_WALL_BOULDER_2, RAND_INF_JABU_JABUS_BELLY_MQ_HOLES_ROOM_WALL_BOULDER_2 },
    { RC_JABU_JABUS_BELLY_MQ_HOLES_ROOM_WALL_BOULDER_3, RAND_INF_JABU_JABUS_BELLY_MQ_HOLES_ROOM_WALL_BOULDER_3 },
    { RC_JABU_JABUS_BELLY_MQ_FORKED_CORRIDOR_BOULDER_1, RAND_INF_JABU_JABUS_BELLY_MQ_FORKED_CORRIDOR_BOULDER_1 },
    { RC_JABU_JABUS_BELLY_MQ_FORKED_CORRIDOR_BOULDER_2, RAND_INF_JABU_JABUS_BELLY_MQ_FORKED_CORRIDOR_BOULDER_2 },
    { RC_JABU_JABUS_BELLY_MQ_TAILPASARAN_BOULDER, RAND_INF_JABU_JABUS_BELLY_MQ_TAILPASARAN_BOULDER },
    { RC_JABU_JABUS_BELLY_MQ_TAILPASARAN_WALL_BOULDER, RAND_INF_JABU_JABUS_BELLY_MQ_TAILPASARAN_WALL_BOULDER },
    { RC_SPIRIT_TEMPLE_MQ_ENTRANCE_BOULDER_1, RAND_INF_SPIRIT_TEMPLE_MQ_ENTRANCE_BOULDER_1 },
    { RC_SPIRIT_TEMPLE_MQ_ENTRANCE_BOULDER_2, RAND_INF_SPIRIT_TEMPLE_MQ_ENTRANCE_BOULDER_2 },
    { RC_SPIRIT_TEMPLE_MQ_ENTRANCE_EYE_BOULDER, RAND_INF_SPIRIT_TEMPLE_MQ_ENTRANCE_EYE_BOULDER },
    { RC_SPIRIT_TEMPLE_MQ_ENTRANCE_CEILING_BOULDER, RAND_INF_SPIRIT_TEMPLE_MQ_ENTRANCE_CEILING_BOULDER },
    { RC_SPIRIT_TEMPLE_MQ_EARLY_ADULT_BOULDER, RAND_INF_SPIRIT_TEMPLE_MQ_EARLY_ADULT_BOULDER },
    { RC_BOTW_MQ_BOULDER_1, RAND_INF_BOTW_MQ_BOULDER_1 },
    { RC_BOTW_MQ_BOULDER_2, RAND_INF_BOTW_MQ_BOULDER_2 },
    { RC_BOTW_MQ_BOULDER_3, RAND_INF_BOTW_MQ_BOULDER_3 },
    { RC_MARKET_TREE, RAND_INF_MARKET_TREE },
    { RC_HC_NEAR_GUARDS_TREE_1, RAND_INF_HC_NEAR_GUARDS_TREE_1 },
    { RC_HC_NEAR_GUARDS_TREE_2, RAND_INF_HC_NEAR_GUARDS_TREE_2 },
    { RC_HC_NEAR_GUARDS_TREE_3, RAND_INF_HC_NEAR_GUARDS_TREE_3 },
    { RC_HC_NEAR_GUARDS_TREE_4, RAND_INF_HC_NEAR_GUARDS_TREE_4 },
    { RC_HC_NEAR_GUARDS_TREE_5, RAND_INF_HC_NEAR_GUARDS_TREE_5 },
    { RC_HC_NEAR_GUARDS_TREE_6, RAND_INF_HC_NEAR_GUARDS_TREE_6 },
    { RC_HC_SKULLTULA_TREE, RAND_INF_HC_SKULLTULA_TREE },
    { RC_HC_GROTTO_TREE, RAND_INF_HC_GROTTO_TREE },
    { RC_HC_NL_TREE_1, RAND_INF_HC_NL_TREE_1 },
    { RC_HC_NL_TREE_2, RAND_INF_HC_NL_TREE_2 },
    { RC_HF_NEAR_KAK_TREE, RAND_INF_HF_NEAR_KAK_TREE },
    { RC_HF_NEAR_KAK_SMALL_TREE, RAND_INF_HF_NEAR_KAK_SMALL_TREE },
    { RC_HF_NEAR_MARKET_TREE_1, RAND_INF_HF_NEAR_MARKET_TREE_1 },
    { RC_HF_NEAR_MARKET_TREE_2, RAND_INF_HF_NEAR_MARKET_TREE_2 },
    { RC_HF_NEAR_MARKET_TREE_3, RAND_INF_HF_NEAR_MARKET_TREE_3 },
    { RC_HF_NEAR_LLR_TREE, RAND_INF_HF_NEAR_LLR_TREE },
    { RC_HF_NEAR_LH_TREE, RAND_INF_HF_NEAR_LH_TREE },
    { RC_HF_CHILD_NEAR_GV_TREE, RAND_INF_HF_CHILD_NEAR_GV_TREE },
    { RC_HF_ADULT_NEAR_GV_TREE, RAND_INF_HF_ADULT_NEAR_GV_TREE },
    { RC_HF_NEAR_ZR_TREE, RAND_INF_HF_NEAR_ZR_TREE },
    { RC_HF_NORTHWEST_TREE_1, RAND_INF_HF_NORTHWEST_TREE_1 },
    { RC_HF_NORTHWEST_TREE_2, RAND_INF_HF_NORTHWEST_TREE_2 },
    { RC_HF_NORTHWEST_TREE_3, RAND_INF_HF_NORTHWEST_TREE_3 },
    { RC_HF_NORTHWEST_TREE_4, RAND_INF_HF_NORTHWEST_TREE_4 },
    { RC_HF_NORTHWEST_TREE_5, RAND_INF_HF_NORTHWEST_TREE_5 },
    { RC_HF_NORTHWEST_TREE_6, RAND_INF_HF_NORTHWEST_TREE_6 },
    { RC_HF_EAST_TREE_1, RAND_INF_HF_EAST_TREE_1 },
    { RC_HF_EAST_TREE_2, RAND_INF_HF_EAST_TREE_2 },
    { RC_HF_EAST_TREE_3, RAND_INF_HF_EAST_TREE_3 },
    { RC_HF_EAST_TREE_4, RAND_INF_HF_EAST_TREE_4 },
    { RC_HF_EAST_TREE_5, RAND_INF_HF_EAST_TREE_5 },
    { RC_HF_EAST_TREE_6, RAND_INF_HF_EAST_TREE_6 },
    { RC_HF_SOUTHEAST_TREE_1, RAND_INF_HF_SOUTHEAST_TREE_1 },
    { RC_HF_SOUTHEAST_TREE_2, RAND_INF_HF_SOUTHEAST_TREE_2 },
    { RC_HF_SOUTHEAST_TREE_3, RAND_INF_HF_SOUTHEAST_TREE_3 },
    { RC_HF_SOUTHEAST_TREE_4, RAND_INF_HF_SOUTHEAST_TREE_4 },
    { RC_HF_SOUTHEAST_TREE_5, RAND_INF_HF_SOUTHEAST_TREE_5 },
    { RC_HF_SOUTHEAST_TREE_6, RAND_INF_HF_SOUTHEAST_TREE_6 },
    { RC_HF_SOUTHEAST_TREE_7, RAND_INF_HF_SOUTHEAST_TREE_7 },
    { RC_HF_SOUTHEAST_TREE_8, RAND_INF_HF_SOUTHEAST_TREE_8 },
    { RC_HF_SOUTHEAST_TREE_9, RAND_INF_HF_SOUTHEAST_TREE_9 },
    { RC_HF_SOUTHEAST_TREE_10, RAND_INF_HF_SOUTHEAST_TREE_10 },
    { RC_HF_SOUTHEAST_TREE_11, RAND_INF_HF_SOUTHEAST_TREE_11 },
    { RC_HF_SOUTHEAST_TREE_12, RAND_INF_HF_SOUTHEAST_TREE_12 },
    { RC_HF_SOUTHEAST_TREE_13, RAND_INF_HF_SOUTHEAST_TREE_13 },
    { RC_HF_SOUTHEAST_TREE_14, RAND_INF_HF_SOUTHEAST_TREE_14 },
    { RC_HF_SOUTHEAST_TREE_15, RAND_INF_HF_SOUTHEAST_TREE_15 },
    { RC_HF_SOUTHEAST_TREE_16, RAND_INF_HF_SOUTHEAST_TREE_16 },
    { RC_HF_SOUTHEAST_TREE_17, RAND_INF_HF_SOUTHEAST_TREE_17 },
    { RC_HF_SOUTHEAST_TREE_18, RAND_INF_HF_SOUTHEAST_TREE_18 },
    { RC_HF_SOUTHEAST_TREE_19, RAND_INF_HF_SOUTHEAST_TREE_19 },
    { RC_HF_CHILD_SOUTHEAST_TREE_1, RAND_INF_HF_CHILD_SOUTHEAST_TREE_1 },
    { RC_HF_CHILD_SOUTHEAST_TREE_2, RAND_INF_HF_CHILD_SOUTHEAST_TREE_2 },
    { RC_HF_CHILD_SOUTHEAST_TREE_3, RAND_INF_HF_CHILD_SOUTHEAST_TREE_3 },
    { RC_HF_CHILD_SOUTHEAST_TREE_4, RAND_INF_HF_CHILD_SOUTHEAST_TREE_4 },
    { RC_HF_CHILD_SOUTHEAST_TREE_5, RAND_INF_HF_CHILD_SOUTHEAST_TREE_5 },
    { RC_HF_CHILD_SOUTHEAST_TREE_6, RAND_INF_HF_CHILD_SOUTHEAST_TREE_6 },
    { RC_HF_TEKTITE_GROTTO_TREE, RAND_INF_HF_TEKTITE_GROTTO_TREE },
    { RC_ZF_TREE, RAND_INF_ZF_TREE },
    { RC_ZR_TREE, RAND_INF_ZR_TREE },
    { RC_KAK_TREE, RAND_INF_KAK_TREE },
    { RC_LLR_TREE, RAND_INF_LLR_TREE },
    { RC_HF_BUSH_NEAR_LAKE_1, RAND_INF_HF_BUSH_NEAR_LAKE_1 },
    { RC_HF_BUSH_NEAR_LAKE_2, RAND_INF_HF_BUSH_NEAR_LAKE_2 },
    { RC_HF_BUSH_NEAR_LAKE_3, RAND_INF_HF_BUSH_NEAR_LAKE_3 },
    { RC_HF_BUSH_NEAR_LAKE_4, RAND_INF_HF_BUSH_NEAR_LAKE_4 },
    { RC_HF_BUSH_NEAR_LAKE_5, RAND_INF_HF_BUSH_NEAR_LAKE_5 },
    { RC_HF_BUSH_NEAR_LAKE_6, RAND_INF_HF_BUSH_NEAR_LAKE_6 },
    { RC_HF_BUSH_NEAR_LAKE_7, RAND_INF_HF_BUSH_NEAR_LAKE_7 },
    { RC_HF_BUSH_NEAR_LAKE_8, RAND_INF_HF_BUSH_NEAR_LAKE_8 },
    { RC_HF_BUSH_NEAR_LAKE_9, RAND_INF_HF_BUSH_NEAR_LAKE_9 },
    { RC_HF_BUSH_NEAR_LAKE_10, RAND_INF_HF_BUSH_NEAR_LAKE_10 },
    { RC_HF_BUSH_NEAR_LAKE_11, RAND_INF_HF_BUSH_NEAR_LAKE_11 },
    { RC_HF_NORTHERN_BUSH_1, RAND_INF_HF_NORTHERN_BUSH_1 },
    { RC_HF_NORTHERN_BUSH_2, RAND_INF_HF_NORTHERN_BUSH_2 },
    { RC_HF_NORTHERN_BUSH_3, RAND_INF_HF_NORTHERN_BUSH_3 },
    { RC_HF_NORTHERN_BUSH_4, RAND_INF_HF_NORTHERN_BUSH_4 },
    { RC_HF_NORTHERN_BUSH_5, RAND_INF_HF_NORTHERN_BUSH_5 },
    { RC_HF_NORTHERN_BUSH_6, RAND_INF_HF_NORTHERN_BUSH_6 },
    { RC_HF_CHILD_NORTHERN_BUSH_1, RAND_INF_HF_CHILD_NORTHERN_BUSH_1 },
    { RC_HF_CHILD_NORTHERN_BUSH_2, RAND_INF_HF_CHILD_NORTHERN_BUSH_2 },
    { RC_HF_CHILD_NORTHERN_BUSH_3, RAND_INF_HF_CHILD_NORTHERN_BUSH_3 },
    { RC_HF_CHILD_NORTHERN_BUSH_4, RAND_INF_HF_CHILD_NORTHERN_BUSH_4 },
    { RC_HF_CHILD_NORTHERN_BUSH_5, RAND_INF_HF_CHILD_NORTHERN_BUSH_5 },
    { RC_HF_CHILD_NORTHERN_BUSH_6, RAND_INF_HF_CHILD_NORTHERN_BUSH_6 },
    { RC_HF_CHILD_NORTHERN_BUSH_7, RAND_INF_HF_CHILD_NORTHERN_BUSH_7 },
    { RC_HF_CHILD_NORTHERN_BUSH_8, RAND_INF_HF_CHILD_NORTHERN_BUSH_8 },
    { RC_HF_CHILD_NORTHERN_BUSH_9, RAND_INF_HF_CHILD_NORTHERN_BUSH_9 },
    { RC_HF_CHILD_NORTHERN_BUSH_10, RAND_INF_HF_CHILD_NORTHERN_BUSH_10 },
    { RC_HF_CHILD_NORTHERN_BUSH_11, RAND_INF_HF_CHILD_NORTHERN_BUSH_11 },
    { RC_HF_BUSH_BY_ROCKY_PATH_1, RAND_INF_HF_BUSH_BY_ROCKY_PATH_1 },
    { RC_HF_BUSH_BY_ROCKY_PATH_2, RAND_INF_HF_BUSH_BY_ROCKY_PATH_2 },
    { RC_HF_BUSH_BY_ROCKY_PATH_3, RAND_INF_HF_BUSH_BY_ROCKY_PATH_3 },
    { RC_HF_BUSH_BY_ROCKY_PATH_4, RAND_INF_HF_BUSH_BY_ROCKY_PATH_4 },
    { RC_HF_BUSH_BY_ROCKY_PATH_5, RAND_INF_HF_BUSH_BY_ROCKY_PATH_5 },
    { RC_HF_BUSH_BY_ROCKY_PATH_6, RAND_INF_HF_BUSH_BY_ROCKY_PATH_6 },
    { RC_HF_SOUTHERN_BUSH_1, RAND_INF_HF_SOUTHERN_BUSH_1 },
    { RC_HF_SOUTHERN_BUSH_2, RAND_INF_HF_SOUTHERN_BUSH_2 },
    { RC_HF_SOUTHERN_BUSH_3, RAND_INF_HF_SOUTHERN_BUSH_3 },
    { RC_HF_SOUTHERN_BUSH_4, RAND_INF_HF_SOUTHERN_BUSH_4 },
    { RC_HF_SOUTHERN_BUSH_5, RAND_INF_HF_SOUTHERN_BUSH_5 },
    { RC_HF_SOUTHERN_BUSH_6, RAND_INF_HF_SOUTHERN_BUSH_6 },
    { RC_HF_SOUTHERN_BUSH_7, RAND_INF_HF_SOUTHERN_BUSH_7 },
    { RC_HF_SOUTHERN_BUSH_8, RAND_INF_HF_SOUTHERN_BUSH_8 },
    { RC_HF_SOUTHERN_BUSH_9, RAND_INF_HF_SOUTHERN_BUSH_9 },
    { RC_HF_SOUTHERN_BUSH_10, RAND_INF_HF_SOUTHERN_BUSH_10 },
    { RC_HF_SOUTHERN_BUSH_11, RAND_INF_HF_SOUTHERN_BUSH_11 },
    { RC_HF_SOUTHERN_BUSH_12, RAND_INF_HF_SOUTHERN_BUSH_12 },
    { RC_HF_CHILD_SOUTHERN_BUSH_1, RAND_INF_HF_CHILD_SOUTHERN_BUSH_1 },
    { RC_HF_CHILD_SOUTHERN_BUSH_2, RAND_INF_HF_CHILD_SOUTHERN_BUSH_2 },
    { RC_HF_CHILD_SOUTHERN_BUSH_3, RAND_INF_HF_CHILD_SOUTHERN_BUSH_3 },
    { RC_HF_CHILD_SOUTHERN_BUSH_4, RAND_INF_HF_CHILD_SOUTHERN_BUSH_4 },
    { RC_HF_CHILD_SOUTHERN_BUSH_5, RAND_INF_HF_CHILD_SOUTHERN_BUSH_5 },
    { RC_HF_CHILD_SOUTHERN_BUSH_6, RAND_INF_HF_CHILD_SOUTHERN_BUSH_6 },
    { RC_HF_CHILD_SOUTHERN_BUSH_7, RAND_INF_HF_CHILD_SOUTHERN_BUSH_7 },
    { RC_HF_CHILD_SOUTHERN_BUSH_8, RAND_INF_HF_CHILD_SOUTHERN_BUSH_8 },
    { RC_HF_CHILD_SOUTHERN_BUSH_9, RAND_INF_HF_CHILD_SOUTHERN_BUSH_9 },
    { RC_HF_CHILD_SOUTHERN_BUSH_10, RAND_INF_HF_CHILD_SOUTHERN_BUSH_10 },
    { RC_HF_CHILD_SOUTHERN_BUSH_11, RAND_INF_HF_CHILD_SOUTHERN_BUSH_11 },
    { RC_HF_CHILD_SOUTHERN_BUSH_12, RAND_INF_HF_CHILD_SOUTHERN_BUSH_12 },
    { RC_ZF_BUSH_1, RAND_INF_ZF_BUSH_1 },
    { RC_ZF_BUSH_2, RAND_INF_ZF_BUSH_2 },
    { RC_ZF_BUSH_3, RAND_INF_ZF_BUSH_3 },
    { RC_ZF_BUSH_4, RAND_INF_ZF_BUSH_4 },
    { RC_ZF_BUSH_5, RAND_INF_ZF_BUSH_5 },
    { RC_ZF_BUSH_6, RAND_INF_ZF_BUSH_6 },
    { RC_KF_DEKU_TREE_RECTANGLE_SIGN, RAND_INF_KF_DEKU_TREE_RECTANGLE_SIGN },
    { RC_KF_STEPPING_STONES_RECTANGLE_SIGN, RAND_INF_KF_STEPPING_STONES_RECTANGLE_SIGN },
    { RC_KF_LINKS_HOUSE_RECTANGLE_SIGN, RAND_INF_KF_LINKS_HOUSE_RECTANGLE_SIGN },
    { RC_KF_FIRST_TRAINING_CENTER_RECTANGLE_SIGN, RAND_INF_KF_FIRST_TRAINING_CENTER_RECTANGLE_SIGN },
    { RC_KF_SECOND_TRAINING_CENTER_RECTANGLE_SIGN, RAND_INF_KF_SECOND_TRAINING_CENTER_RECTANGLE_SIGN },
    { RC_KF_AFTER_CRAWLSPACE_RECTANGLE_SIGN, RAND_INF_KF_AFTER_CRAWLSPACE_RECTANGLE_SIGN },
    { RC_KF_CRAWL_RECTANGLE_RECTANGLE_SIGN, RAND_INF_KF_CRAWL_RECTANGLE_RECTANGLE_SIGN },
    { RC_KF_LOST_WOODS_RECTANGLE_SIGN, RAND_INF_KF_LOST_WOODS_RECTANGLE_SIGN },
    { RC_KF_HOUSE_OF_TWINS_ARROW_SIGN, RAND_INF_KF_HOUSE_OF_TWINS_ARROW_SIGN },
    { RC_KF_SHOP_ARROW_SIGN, RAND_INF_KF_SHOP_ARROW_SIGN },
    { RC_KF_SARIAS_HOUSE_ARROW_SIGN, RAND_INF_KF_SARIAS_HOUSE_ARROW_SIGN },
    { RC_KF_LOST_WOODS_ARROW_SIGN, RAND_INF_KF_LOST_WOODS_ARROW_SIGN },
    { RC_KF_MIDOS_HOUSE_ARROW_SIGN, RAND_INF_KF_MIDOS_HOUSE_ARROW_SIGN },
    { RC_KF_TRAINING_CENTER_ENTRANCE_ARROW_SIGN, RAND_INF_KF_TRAINING_CENTER_ENTRANCE_ARROW_SIGN },
    { RC_KF_INNER_TRAINING_CENTER_ARROW_SIGN, RAND_INF_KF_INNER_TRAINING_CENTER_ARROW_SIGN },
    { RC_KF_KNOW_IT_ALL_BROTHERS_HOUSE_ARROW_SIGN, RAND_INF_KF_KNOW_IT_ALL_BROTHERS_HOUSE_ARROW_SIGN },
    { RC_KF_BOULDER_MAZE_RECTANGLE_SIGN, RAND_INF_KF_BOULDER_MAZE_RECTANGLE_SIGN },
    { RC_KF_LINKS_HOUSE_SIGN, RAND_INF_KF_LINKS_HOUSE_SIGN },
    { RC_LW_THEATER_RECTANGLE_SIGN, RAND_INF_LW_THEATER_RECTANGLE_SIGN },
    { RC_HF_CASTLE_EXIT_ARROW_SIGN, RAND_INF_HF_CASTLE_EXIT_ARROW_SIGN },
    { RC_HF_WOODED_EXIT_ARROW_SIGN, RAND_INF_HF_WOODED_EXIT_ARROW_SIGN },
    { RC_HF_ROCKY_PATH_EXIT_ARROW_SIGN, RAND_INF_HF_ROCKY_PATH_EXIT_ARROW_SIGN },
    { RC_HF_FENCED_ARROW_SIGN, RAND_INF_HF_FENCED_ARROW_SIGN },
    { RC_HF_CENTER_EXIT_ARROW_SIGN, RAND_INF_HF_CENTER_EXIT_ARROW_SIGN },
    { RC_HF_RIVER_EXIT_ARROW_SIGN, RAND_INF_HF_RIVER_EXIT_ARROW_SIGN },
    { RC_HF_STAIRS_EXIT_ARROW_SIGN, RAND_INF_HF_STAIRS_EXIT_ARROW_SIGN },
    { RC_MK_SHOOTING_GALLERY_RECTANGLE_SIGN, RAND_INF_MK_SHOOTING_GALLERY_RECTANGLE_SIGN },
    { RC_MK_MASK_SHOP_SIGN, RAND_INF_MK_MASK_SHOP_SIGN },
    { RC_TOT_ALTAR, RAND_INF_TOT_ALTAR },
    { RC_HC_DEAD_END_RECTANGLE_SIGN, RAND_INF_HC_DEAD_END_RECTANGLE_SIGN },
    { RC_KAK_GUARD_GATE_RECTANGLE_SIGN, RAND_INF_KAK_GUARD_GATE_RECTANGLE_SIGN },
    { RC_KAK_WELL_RECTANGLE_SIGN, RAND_INF_KAK_WELL_RECTANGLE_SIGN },
    { RC_KAK_SOUTHEAST_EXIT_ARROW_SIGN, RAND_INF_KAK_SOUTHEAST_EXIT_ARROW_SIGN },
    { RC_KAK_FRONT_GATE_ARROW_SIGN, RAND_INF_KAK_FRONT_GATE_ARROW_SIGN },
    { RC_KAK_SHOOTING_GALLERY_RECTANGLE_SIGN, RAND_INF_KAK_SHOOTING_GALLERY_RECTANGLE_SIGN },
    { RC_GY_ENTRANCE_RECTANGLE_SIGN, RAND_INF_GY_ENTRANCE_RECTANGLE_SIGN },
    { RC_GY_ENTRANCE_PLINTH, RAND_INF_GY_ENTRANCE_PLINTH },
    { RC_GY_RIGHT_OF_ROYAL_TOMB_GRAVE, RAND_INF_GY_RIGHT_OF_ROYAL_TOMB_GRAVE },
    { RC_GY_LEFT_OF_ROYAL_TOMB_GRAVE, RAND_INF_GY_LEFT_OF_ROYAL_TOMB_GRAVE },
    { RC_GY_ROYAL_TOMB_GRAVE, RAND_INF_GY_ROYAL_TOMB_GRAVE },
    { RC_DMT_ABOVE_DODONGO_RECTANGLE_SIGN, RAND_INF_DMT_ABOVE_DODONGO_RECTANGLE_SIGN },
    { RC_DMT_ADULT_CENTER_EXIT_ARROW_SIGN, RAND_INF_DMT_ADULT_CENTER_EXIT_ARROW_SIGN },
    { RC_DMT_CHILD_CENTER_EXIT_RECTANGLE_SIGN, RAND_INF_DMT_CHILD_CENTER_EXIT_RECTANGLE_SIGN },
    { RC_DMT_DODONGOS_CAVERN_RECTANGLE_SIGN, RAND_INF_DMT_DODONGOS_CAVERN_RECTANGLE_SIGN },
    { RC_DMT_CENTER_TRAIL_RECTANGLE_SIGN, RAND_INF_DMT_CENTER_TRAIL_RECTANGLE_SIGN },
    { RC_DMT_TO_UPPER_TRAIL_ARROW_SIGN, RAND_INF_DMT_TO_UPPER_TRAIL_ARROW_SIGN },
    { RC_DMT_UPPER_EXIT_ARROW_SIGN, RAND_INF_DMT_UPPER_EXIT_ARROW_SIGN },
    { RC_DMT_TO_CENTER_EXIT_ARROW_SIGN, RAND_INF_DMT_TO_CENTER_EXIT_ARROW_SIGN },
    { RC_DMT_LOWER_EXIT_ARROW_SIGN, RAND_INF_DMT_LOWER_EXIT_ARROW_SIGN },
    { RC_GC_CHILD_ROLLING_GORON_RECTANGLE_SIGN, RAND_INF_GC_CHILD_ROLLING_GORON_RECTANGLE_SIGN },
    { RC_DMC_BRIDGE_EXIT_ARROW_SIGN, RAND_INF_DMC_BRIDGE_EXIT_ARROW_SIGN },
    { RC_ZR_SLEEPLESS_WATERFALL_PLAQUE, RAND_INF_ZR_SLEEPLESS_WATERFALL_PLAQUE },
    { RC_ZD_SHOP_RECTANGLE_SIGN, RAND_INF_ZD_SHOP_RECTANGLE_SIGN },
    { RC_ZD_ENTRANCE_RECTANGLE_SIGN, RAND_INF_ZD_ENTRANCE_RECTANGLE_SIGN },
    { RC_ZD_KING_ZORA_PATH_ARROW_SIGN, RAND_INF_ZD_KING_ZORA_PATH_ARROW_SIGN },
    { RC_ZD_NEAR_KING_ZORA_RECTANGLE_SIGN, RAND_INF_ZD_NEAR_KING_ZORA_RECTANGLE_SIGN },
    { RC_ZD_NEAR_KING_ZORA_ARROW_SIGN, RAND_INF_ZD_NEAR_KING_ZORA_ARROW_SIGN },
    { RC_ZF_JABU_JABU_PLATFORM_RECTANGLE_SIGN, RAND_INF_ZF_JABU_JABU_PLATFORM_RECTANGLE_SIGN },
    { RC_ZF_ENTRANCE_ARROW_SIGN, RAND_INF_ZF_ENTRANCE_ARROW_SIGN },
    { RC_LH_LAB_RECTANGLE_SIGN, RAND_INF_LH_LAB_RECTANGLE_SIGN },
    { RC_LH_NORTH_EXIT_ARROW_SIGN, RAND_INF_LH_NORTH_EXIT_ARROW_SIGN },
    { RC_LH_FISHING_SIGN, RAND_INF_LH_FISHING_SIGN },
    { RC_LH_ISLAND_PEDESTAL, RAND_INF_LH_ISLAND_PEDESTAL },
    { RC_LH_FISHING_POND_RECTANGLE_SIGN, RAND_INF_LH_FISHING_POND_RECTANGLE_SIGN },
    { RC_GV_BRIDGE_RECTANGLE_SIGN, RAND_INF_GV_BRIDGE_RECTANGLE_SIGN },
    { RC_GV_EAST_EXIT_ARROW_SIGN, RAND_INF_GV_EAST_EXIT_ARROW_SIGN },
    { RC_GF_EAST_EXIT_ARROW_SIGN, RAND_INF_GF_EAST_EXIT_ARROW_SIGN },
    { RC_GF_HBA_RECTANGLE_SIGN, RAND_INF_GF_HBA_RECTANGLE_SIGN },
    { RC_GF_GATE_EXIT_RECTANGLE_SIGN, RAND_INF_GF_GATE_EXIT_RECTANGLE_SIGN },
    { RC_GF_GTG_ENTRANCE_RECTANGLE_SIGN, RAND_INF_GF_GTG_ENTRANCE_RECTANGLE_SIGN },
    { RC_HW_CARPET_SALESMAN_ARROW_SIGN, RAND_INF_HW_CARPET_SALESMAN_ARROW_SIGN },
    { RC_HW_POE_ALTAR, RAND_INF_HW_POE_ALTAR },
    { RC_DODONGOS_CAVERN_TOP_FLOOR_PEDESTAL, RAND_INF_DODONGOS_CAVERN_TOP_FLOOR_PEDESTAL },
    { RC_SHADOW_TEMPLE_TRUTHSPINNER_RECTANGLE_SIGN, RAND_INF_SHADOW_TEMPLE_TRUTHSPINNER_RECTANGLE_SIGN },
    { RC_SHADOW_TEMPLE_FALLING_SPIKES_RECTANGLE_SIGN, RAND_INF_SHADOW_TEMPLE_FALLING_SPIKES_RECTANGLE_SIGN },
    { RC_SPIRIT_TEMPLE_LEFT_SNAKE_STATUE, RAND_INF_SPIRIT_TEMPLE_LEFT_SNAKE_STATUE },
    { RC_SPIRIT_TEMPLE_RIGHT_SNAKE_STATUE, RAND_INF_SPIRIT_TEMPLE_RIGHT_SNAKE_STATUE },
    { RC_SHADOW_TEMPLE_MQ_LOWER_PIT_RECTANGLE_SIGN, RAND_INF_SHADOW_TEMPLE_MQ_LOWER_PIT_RECTANGLE_SIGN },
    // Wonder Items
    { RC_KF_WONDER_TRAINING_1, RAND_INF_KF_WONDER_TRAINING_1 },
    { RC_KF_WONDER_TRAINING_2, RAND_INF_KF_WONDER_TRAINING_2 },
    { RC_KF_WONDER_TRAINING_3, RAND_INF_KF_WONDER_TRAINING_3 },
    { RC_KF_WONDER_SHOP, RAND_INF_KF_WONDER_SHOP },
    { RC_KF_WONDER_SIGN, RAND_INF_KF_WONDER_SIGN },
    { RC_KF_WONDER_PLATFORMS_1, RAND_INF_KF_WONDER_PLATFORMS_1 },
    { RC_KF_WONDER_PLATFORMS_2, RAND_INF_KF_WONDER_PLATFORMS_2 },
    { RC_KF_WONDER_CRAWL_GRASS_1, RAND_INF_KF_WONDER_CRAWL_GRASS_1 },
    { RC_KF_WONDER_CRAWL_GRASS_2, RAND_INF_KF_WONDER_CRAWL_GRASS_2 },
    { RC_HF_WONDER_BRIDGE_1, RAND_INF_HF_WONDER_BRIDGE_1 },
    { RC_HF_WONDER_BRIDGE_2, RAND_INF_HF_WONDER_BRIDGE_2 },
    { RC_HF_WONDER_BRIDGE_3, RAND_INF_HF_WONDER_BRIDGE_3 },
    { RC_MKT_WONDER_DAY_1, RAND_INF_MKT_WONDER_DAY_1 },
    { RC_MKT_WONDER_DAY_2, RAND_INF_MKT_WONDER_DAY_2 },
    { RC_MKT_WONDER_DAY_3, RAND_INF_MKT_WONDER_DAY_3 },
    { RC_MKT_WONDER_DAY_4, RAND_INF_MKT_WONDER_DAY_4 },
    { RC_MKT_WONDER_DAY_5, RAND_INF_MKT_WONDER_DAY_5 },
    { RC_MKT_WONDER_NIGHT_1, RAND_INF_MKT_WONDER_NIGHT_1 },
    { RC_MKT_WONDER_NIGHT_2, RAND_INF_MKT_WONDER_NIGHT_2 },
    { RC_LLR_WONDER_BIG_FENCE, RAND_INF_LLR_WONDER_BIG_FENCE },
    { RC_LLR_WONDER_SMALL_FENCE, RAND_INF_LLR_WONDER_SMALL_FENCE },
    { RC_HC_WONDER_LEFT_TORCH, RAND_INF_HC_WONDER_LEFT_TORCH },
    { RC_HC_WONDER_RIGHT_TORCH, RAND_INF_HC_WONDER_RIGHT_TORCH },
    { RC_HC_WONDER_MOAT_1, RAND_INF_HC_WONDER_MOAT_1 },
    { RC_HC_WONDER_MOAT_2, RAND_INF_HC_WONDER_MOAT_2 },
    { RC_HC_WONDER_MOAT_3, RAND_INF_HC_WONDER_MOAT_3 },
    { RC_HC_WONDER_MOAT_4, RAND_INF_HC_WONDER_MOAT_4 },
    { RC_HC_WONDER_MOAT_5, RAND_INF_HC_WONDER_MOAT_5 },
    { RC_HC_WONDER_MOAT_6, RAND_INF_HC_WONDER_MOAT_6 },
    { RC_HC_WONDER_MOAT_7, RAND_INF_HC_WONDER_MOAT_7 },
    { RC_HC_WONDER_MOAT_8, RAND_INF_HC_WONDER_MOAT_8 },
    { RC_HC_WONDER_MOAT_9, RAND_INF_HC_WONDER_MOAT_9 },
    { RC_HC_WONDER_MOAT_10, RAND_INF_HC_WONDER_MOAT_10 },
    { RC_HC_WONDER_COURTYARD_RIGHT_WINDOW, RAND_INF_HC_WONDER_COURTYARD_RIGHT_WINDOW },
    { RC_HC_WONDER_COURTYARD_LEFT_WINDOW, RAND_INF_HC_WONDER_COURTYARD_LEFT_WINDOW },
    { RC_LW_WONDER_BACK_SKULL_KIDS_GRASS_1, RAND_INF_LW_WONDER_BACK_SKULL_KIDS_GRASS_1 },
    { RC_LW_WONDER_BACK_SKULL_KIDS_GRASS_2, RAND_INF_LW_WONDER_BACK_SKULL_KIDS_GRASS_2 },
    { RC_LW_WONDER_FRONT_SKULL_KIDS_GRASS, RAND_INF_LW_WONDER_FRONT_SKULL_KIDS_GRASS },
    { RC_SFM_WONDER_ENTRANCE, RAND_INF_SFM_WONDER_ENTRANCE },
    { RC_SFM_WONDER_MAZE_1, RAND_INF_SFM_WONDER_MAZE_1 },
    { RC_SFM_WONDER_MAZE_2, RAND_INF_SFM_WONDER_MAZE_2 },
    { RC_SFM_WONDER_MAZE_3, RAND_INF_SFM_WONDER_MAZE_3 },
    { RC_SFM_WONDER_MAZE_4, RAND_INF_SFM_WONDER_MAZE_4 },
    { RC_SFM_WONDER_MAZE_5, RAND_INF_SFM_WONDER_MAZE_5 },
    { RC_KAK_WONDER_UNDER_CONSTRUCTION, RAND_INF_KAK_WONDER_UNDER_CONSTRUCTION },
    { RC_KAK_WONDER_ABOVE_COW, RAND_INF_KAK_WONDER_ABOVE_COW },
    { RC_GY_WONDER_DAMPE_RACE_1, RAND_INF_GY_WONDER_DAMPE_RACE_1 },
    { RC_GY_WONDER_DAMPE_RACE_2, RAND_INF_GY_WONDER_DAMPE_RACE_2 },
    { RC_GY_WONDER_DAMPE_RACE_3, RAND_INF_GY_WONDER_DAMPE_RACE_3 },
    { RC_GY_WONDER_DAMPE_RACE_4, RAND_INF_GY_WONDER_DAMPE_RACE_4 },
    { RC_GY_WONDER_DAMPE_RACE_5, RAND_INF_GY_WONDER_DAMPE_RACE_5 },
    { RC_GY_WONDER_DAMPE_RACE_6, RAND_INF_GY_WONDER_DAMPE_RACE_6 },
    { RC_GY_WONDER_DAMPE_RACE_7, RAND_INF_GY_WONDER_DAMPE_RACE_7 },
    { RC_GY_WONDER_DAMPE_RACE_8, RAND_INF_GY_WONDER_DAMPE_RACE_8 },
    { RC_GY_WONDER_DAMPE_RACE_9, RAND_INF_GY_WONDER_DAMPE_RACE_9 },
    { RC_GY_WONDER_DAMPE_RACE_10, RAND_INF_GY_WONDER_DAMPE_RACE_10 },
    { RC_GY_WONDER_DAMPE_RACE_11, RAND_INF_GY_WONDER_DAMPE_RACE_11 },
    { RC_GY_WONDER_DAMPE_RACE_12, RAND_INF_GY_WONDER_DAMPE_RACE_12 },
    { RC_GY_WONDER_DAMPE_RACE_13, RAND_INF_GY_WONDER_DAMPE_RACE_13 },
    { RC_GY_WONDER_DAMPE_RACE_14, RAND_INF_GY_WONDER_DAMPE_RACE_14 },
    { RC_GY_WONDER_DAMPE_RACE_15, RAND_INF_GY_WONDER_DAMPE_RACE_15 },
    { RC_DMC_WONDER_BENEATH_BRIDGE_PLATFORM, RAND_INF_DMC_WONDER_BENEATH_BRIDGE_PLATFORM },
    { RC_ZR_WONDER_NEAR_DOMAIN_1, RAND_INF_ZR_WONDER_NEAR_DOMAIN_1 },
    { RC_ZR_WONDER_NEAR_DOMAIN_2, RAND_INF_ZR_WONDER_NEAR_DOMAIN_2 },
    { RC_ZR_WONDER_NEAR_DOMAIN_3, RAND_INF_ZR_WONDER_NEAR_DOMAIN_3 },
    { RC_ZR_WONDER_NEAR_DOMAIN_4, RAND_INF_ZR_WONDER_NEAR_DOMAIN_4 },
    { RC_ZR_WONDER_BEFORE_LADDER_1, RAND_INF_ZR_WONDER_BEFORE_LADDER_1 },
    { RC_ZR_WONDER_BEFORE_LADDER_2, RAND_INF_ZR_WONDER_BEFORE_LADDER_2 },
    { RC_ZR_WONDER_BEFORE_LADDER_3, RAND_INF_ZR_WONDER_BEFORE_LADDER_3 },
    { RC_ZR_WONDER_BEFORE_LADDER_4, RAND_INF_ZR_WONDER_BEFORE_LADDER_4 },
    { RC_ZR_WONDER_BEFORE_LADDER_5, RAND_INF_ZR_WONDER_BEFORE_LADDER_5 },
    { RC_ZR_WONDER_BEFORE_LADDER_6, RAND_INF_ZR_WONDER_BEFORE_LADDER_6 },
    { RC_ZR_WONDER_AFTER_LADDER_1, RAND_INF_ZR_WONDER_AFTER_LADDER_1 },
    { RC_ZR_WONDER_AFTER_LADDER_2, RAND_INF_ZR_WONDER_AFTER_LADDER_2 },
    { RC_ZR_WONDER_AFTER_LADDER_3, RAND_INF_ZR_WONDER_AFTER_LADDER_3 },
    { RC_ZR_WONDER_FROG_BRIDGE_1, RAND_INF_ZR_WONDER_FROG_BRIDGE_1 },
    { RC_ZR_WONDER_FROG_BRIDGE_2, RAND_INF_ZR_WONDER_FROG_BRIDGE_2 },
    { RC_ZR_WONDER_FROG_BRIDGE_3, RAND_INF_ZR_WONDER_FROG_BRIDGE_3 },
    { RC_ZR_WONDER_PILLARS_1, RAND_INF_ZR_WONDER_PILLARS_1 },
    { RC_ZR_WONDER_PILLARS_2, RAND_INF_ZR_WONDER_PILLARS_2 },
    { RC_ZR_WONDER_PILLARS_3, RAND_INF_ZR_WONDER_PILLARS_3 },
    { RC_ZR_WONDER_PILLARS_4, RAND_INF_ZR_WONDER_PILLARS_4 },
    { RC_ZR_WONDER_LOWER_LAND_BRIDGE_1, RAND_INF_ZR_WONDER_LOWER_LAND_BRIDGE_1 },
    { RC_ZR_WONDER_LOWER_LAND_BRIDGE_2, RAND_INF_ZR_WONDER_LOWER_LAND_BRIDGE_2 },
    { RC_ZR_WONDER_LOWER_LAND_BRIDGE_3, RAND_INF_ZR_WONDER_LOWER_LAND_BRIDGE_3 },
    { RC_ZR_WONDER_LOWER_LAND_BRIDGE_4, RAND_INF_ZR_WONDER_LOWER_LAND_BRIDGE_4 },
    { RC_ZR_WONDER_NEAR_CUCCO_1, RAND_INF_ZR_WONDER_NEAR_CUCCO_1 },
    { RC_ZR_WONDER_NEAR_CUCCO_2, RAND_INF_ZR_WONDER_NEAR_CUCCO_2 },
    { RC_ZR_WONDER_NEAR_CUCCO_3, RAND_INF_ZR_WONDER_NEAR_CUCCO_3 },
    { RC_ZR_WONDER_LOWER_RIVER_1, RAND_INF_ZR_WONDER_LOWER_RIVER_1 },
    { RC_ZR_WONDER_LOWER_RIVER_2, RAND_INF_ZR_WONDER_LOWER_RIVER_2 },
    { RC_ZR_WONDER_LOWER_RIVER_3, RAND_INF_ZR_WONDER_LOWER_RIVER_3 },
    { RC_ZR_WONDER_LOWER_RIVER_4, RAND_INF_ZR_WONDER_LOWER_RIVER_4 },
    { RC_ZF_WONDER_ROCK, RAND_INF_ZF_WONDER_ROCK },
    { RC_GV_WONDER_LOWER_WATERFALL, RAND_INF_GV_WONDER_LOWER_WATERFALL },
    { RC_GV_WONDER_UPPER_WATERFALL, RAND_INF_GV_WONDER_UPPER_WATERFALL },
    { RC_GF_WONDER_ENTRANCE_SIGN, RAND_INF_GF_WONDER_ENTRANCE_SIGN },
    { RC_GF_WONDER_ARCHERY_SIGN, RAND_INF_GF_WONDER_ARCHERY_SIGN },
    { RC_TH_WONDER_1_TORCH_1, RAND_INF_TH_WONDER_1_TORCH_1 },
    { RC_TH_WONDER_1_TORCH_2, RAND_INF_TH_WONDER_1_TORCH_2 },
    { RC_TH_WONDER_STEEP_SLOPE_LOWER_EXIT, RAND_INF_TH_WONDER_STEEP_SLOPE_LOWER_EXIT },
    { RC_TH_WONDER_STEEP_SLOPE_UPPER_EXIT, RAND_INF_TH_WONDER_STEEP_SLOPE_UPPER_EXIT },
    { RC_TH_WONDER_DOUBLE_JAIL_LOWER_EXIT, RAND_INF_TH_WONDER_DOUBLE_JAIL_LOWER_EXIT },
    { RC_TH_WONDER_DOUBLE_JAIL_UPPER_EXIT, RAND_INF_TH_WONDER_DOUBLE_JAIL_UPPER_EXIT },
    { RC_TH_WONDER_KITCHEN_SKULL, RAND_INF_TH_WONDER_KITCHEN_SKULL },
    { RC_TH_WONDER_KITCHEN_SOUP, RAND_INF_TH_WONDER_KITCHEN_SOUP },
    { RC_TH_WONDER_DEAD_END_SKULL_ENTRANCE, RAND_INF_TH_WONDER_DEAD_END_SKULL_ENTRANCE },
    { RC_TH_WONDER_DEAD_END_SKULL_NEAR_JAIL, RAND_INF_TH_WONDER_DEAD_END_SKULL_NEAR_JAIL },
    { RC_TH_WONDER_BREAK_ROOM_BOTTOM_SKULL, RAND_INF_TH_WONDER_BREAK_ROOM_BOTTOM_SKULL },
    { RC_TH_WONDER_BREAK_ROOM_TOP_SKULL, RAND_INF_TH_WONDER_BREAK_ROOM_TOP_SKULL },
    { RC_COLOSSUS_WONDER_OASIS_TREE_1, RAND_INF_COLOSSUS_WONDER_OASIS_TREE_1 },
    { RC_COLOSSUS_WONDER_OASIS_TREE_2, RAND_INF_COLOSSUS_WONDER_OASIS_TREE_2 },
    { RC_COLOSSUS_WONDER_OASIS_CHILD_TREE, RAND_INF_COLOSSUS_WONDER_OASIS_CHILD_TREE },
    { RC_COLOSSUS_WONDER_GF_TREE_1, RAND_INF_COLOSSUS_WONDER_GF_TREE_1 },
    { RC_COLOSSUS_WONDER_GF_TREE_2, RAND_INF_COLOSSUS_WONDER_GF_TREE_2 },
    { RC_SHADOW_TEMPLE_WONDER_THREE_POTS, RAND_INF_SHADOW_TEMPLE_WONDER_THREE_POTS },
    { RC_GERUDO_TRAINING_GROUND_WONDER_BEAMOS_ROOM, RAND_INF_GERUDO_TRAINING_GROUND_WONDER_BEAMOS_ROOM },
    { RC_GERUDO_TRAINING_GROUND_WONDER_EYE_STATUE_ROOM, RAND_INF_GERUDO_TRAINING_GROUND_WONDER_EYE_STATUE_ROOM },
    { RC_GERUDO_TRAINING_GROUND_WONDER_TORCH_SLUGS_ROOM, RAND_INF_GERUDO_TRAINING_GROUND_WONDER_TORCH_SLUGS_ROOM },
    { RC_DEKU_TREE_MQ_WONDER_BASEMENT_GRAVE_1, RAND_INF_DEKU_TREE_MQ_WONDER_BASEMENT_GRAVE_1 },
    { RC_DEKU_TREE_MQ_WONDER_BASEMENT_GRAVE_2, RAND_INF_DEKU_TREE_MQ_WONDER_BASEMENT_GRAVE_2 },
    { RC_DEKU_TREE_MQ_WONDER_BASEMENT_GRAVE_3, RAND_INF_DEKU_TREE_MQ_WONDER_BASEMENT_GRAVE_3 },
    { RC_DEKU_TREE_MQ_WONDER_BASEMENT_GRAVE_4, RAND_INF_DEKU_TREE_MQ_WONDER_BASEMENT_GRAVE_4 },
    { RC_JABU_JABUS_BELLY_MQ_WONDER_ENTRANCE_LEFT_COW, RAND_INF_JABU_JABUS_BELLY_MQ_WONDER_ENTRANCE_LEFT_COW },
    { RC_JABU_JABUS_BELLY_MQ_WONDER_ENTRANCE_RIGHT_COW, RAND_INF_JABU_JABUS_BELLY_MQ_WONDER_ENTRANCE_RIGHT_COW },
    { RC_JABU_JABUS_BELLY_MQ_WONDER_ELEVATOR_COW, RAND_INF_JABU_JABUS_BELLY_MQ_WONDER_ELEVATOR_COW },
    { RC_JABU_JABUS_BELLY_MQ_HOLES_COW, RAND_INF_JABU_JABUS_BELLY_MQ_HOLES_COW },
    { RC_JABU_JABUS_BELLY_MQ_WONDER_BASEMENT_RIGHT_COW_1, RAND_INF_JABU_JABUS_BELLY_MQ_WONDER_BASEMENT_RIGHT_COW_1 },
    { RC_JABU_JABUS_BELLY_MQ_WONDER_BASEMENT_RIGHT_COW_2, RAND_INF_JABU_JABUS_BELLY_MQ_WONDER_BASEMENT_RIGHT_COW_2 },
    { RC_JABU_JABUS_BELLY_MQ_WONDER_BASEMENT_RIGHT_COW_3, RAND_INF_JABU_JABUS_BELLY_MQ_WONDER_BASEMENT_RIGHT_COW_3 },
    { RC_JABU_JABUS_BELLY_MQ_WONDER_BASEMENT_LEFT_COW_1, RAND_INF_JABU_JABUS_BELLY_MQ_WONDER_BASEMENT_LEFT_COW_1 },
    { RC_JABU_JABUS_BELLY_MQ_WONDER_BASEMENT_LEFT_COW_2, RAND_INF_JABU_JABUS_BELLY_MQ_WONDER_BASEMENT_LEFT_COW_2 },
    { RC_JABU_JABUS_BELLY_MQ_WONDER_BASEMENT_LEFT_COW_3, RAND_INF_JABU_JABUS_BELLY_MQ_WONDER_BASEMENT_LEFT_COW_3 },
    { RC_JABU_JABUS_BELLY_MQ_WONDER_AFTER_BIG_OCTO, RAND_INF_JABU_JABUS_BELLY_MQ_WONDER_AFTER_BIG_OCTO },
    { RC_JABU_JABUS_BELLY_MQ_WONDER_JIGGLIES_COW, RAND_INF_JABU_JABUS_BELLY_MQ_WONDER_JIGGLIES_COW },
    { RC_JABU_JABUS_BELLY_MQ_WONDER_FALLING_LIKE_LIKES_COW_RIGHT_1,
      RAND_INF_JABU_JABUS_BELLY_MQ_WONDER_FALLING_LIKE_LIKES_COW_RIGHT_1 },
    { RC_JABU_JABUS_BELLY_MQ_WONDER_FALLING_LIKE_LIKES_COW_RIGHT_2,
      RAND_INF_JABU_JABUS_BELLY_MQ_WONDER_FALLING_LIKE_LIKES_COW_RIGHT_2 },
    { RC_JABU_JABUS_BELLY_MQ_WONDER_FALLING_LIKE_LIKES_COW_RIGHT_3,
      RAND_INF_JABU_JABUS_BELLY_MQ_WONDER_FALLING_LIKE_LIKES_COW_RIGHT_3 },
    { RC_JABU_JABUS_BELLY_MQ_WONDER_FALLING_LIKE_LIKES_COW_LEFT_1,
      RAND_INF_JABU_JABUS_BELLY_MQ_WONDER_FALLING_LIKE_LIKES_COW_LEFT_1 },
    { RC_JABU_JABUS_BELLY_MQ_WONDER_FALLING_LIKE_LIKES_COW_LEFT_2,
      RAND_INF_JABU_JABUS_BELLY_MQ_WONDER_FALLING_LIKE_LIKES_COW_LEFT_2 },
    { RC_JABU_JABUS_BELLY_MQ_WONDER_FALLING_LIKE_LIKES_COW_LEFT_3,
      RAND_INF_JABU_JABUS_BELLY_MQ_WONDER_FALLING_LIKE_LIKES_COW_LEFT_3 },
    { RC_JABU_JABUS_BELLY_MQ_WONDER_FALLING_LIKE_LIKES_EXPLOSION_1,
      RAND_INF_JABU_JABUS_BELLY_MQ_WONDER_FALLING_LIKE_LIKES_EXPLOSION_1 },
    { RC_JABU_JABUS_BELLY_MQ_WONDER_FALLING_LIKE_LIKES_EXPLOSION_2,
      RAND_INF_JABU_JABUS_BELLY_MQ_WONDER_FALLING_LIKE_LIKES_EXPLOSION_2 },
    { RC_JABU_JABUS_BELLY_MQ_WONDER_FALLING_LIKE_LIKES_EXPLOSION_3,
      RAND_INF_JABU_JABUS_BELLY_MQ_WONDER_FALLING_LIKE_LIKES_EXPLOSION_3 },
    { RC_JABU_JABUS_BELLY_MQ_WONDER_BEFORE_BOSS_LEFT_COW, RAND_INF_JABU_JABUS_BELLY_MQ_WONDER_BEFORE_BOSS_LEFT_COW },
    { RC_JABU_JABUS_BELLY_MQ_WONDER_BEFORE_BOSS_RIGHT_COW_1,
      RAND_INF_JABU_JABUS_BELLY_MQ_WONDER_BEFORE_BOSS_RIGHT_COW_1 },
    { RC_JABU_JABUS_BELLY_MQ_WONDER_BEFORE_BOSS_RIGHT_COW_2,
      RAND_INF_JABU_JABUS_BELLY_MQ_WONDER_BEFORE_BOSS_RIGHT_COW_2 },
    { RC_FIRE_TEMPLE_MQ_WONDER_SHORTCUT_ROOM_1, RAND_INF_FIRE_TEMPLE_MQ_WONDER_SHORTCUT_ROOM_1 },
    { RC_FIRE_TEMPLE_MQ_WONDER_SHORTCUT_ROOM_2, RAND_INF_FIRE_TEMPLE_MQ_WONDER_SHORTCUT_ROOM_2 },
    { RC_FIRE_TEMPLE_MQ_WONDER_SHORTCUT_ROOM_3, RAND_INF_FIRE_TEMPLE_MQ_WONDER_SHORTCUT_ROOM_3 },
    { RC_FIRE_TEMPLE_MQ_WONDER_BOSS_KEY_ROOM_HOOKSHOT, RAND_INF_FIRE_TEMPLE_MQ_WONDER_BOSS_KEY_ROOM_HOOKSHOT },
    { RC_FIRE_TEMPLE_MQ_WONDER_BOSS_KEY_ROOM_BOW, RAND_INF_FIRE_TEMPLE_MQ_WONDER_BOSS_KEY_ROOM_BOW },
    { RC_FIRE_TEMPLE_MQ_WONDER_LIZALFOS_MAZE, RAND_INF_FIRE_TEMPLE_MQ_WONDER_LIZALFOS_MAZE },
    { RC_FIRE_TEMPLE_MQ_WONDER_EAST_TOWER_LARGE_FACE_1, RAND_INF_FIRE_TEMPLE_MQ_WONDER_EAST_TOWER_LARGE_FACE_1 },
    { RC_FIRE_TEMPLE_MQ_WONDER_EAST_TOWER_LARGE_FACE_2, RAND_INF_FIRE_TEMPLE_MQ_WONDER_EAST_TOWER_LARGE_FACE_2 },
    { RC_FIRE_TEMPLE_MQ_WONDER_EAST_TOWER_SMALL_FACE_1, RAND_INF_FIRE_TEMPLE_MQ_WONDER_EAST_TOWER_SMALL_FACE_1 },
    { RC_FIRE_TEMPLE_MQ_WONDER_EAST_TOWER_SMALL_FACE_2, RAND_INF_FIRE_TEMPLE_MQ_WONDER_EAST_TOWER_SMALL_FACE_2 },
    { RC_FIRE_TEMPLE_MQ_WONDER_TORCH_ROOM, RAND_INF_FIRE_TEMPLE_MQ_WONDER_TORCH_ROOM },
    { RC_FIRE_TEMPLE_MQ_WONDER_FIRE_MAZE, RAND_INF_FIRE_TEMPLE_MQ_WONDER_FIRE_MAZE },
    { RC_FIRE_TEMPLE_MQ_WONDER_AFTER_FLARE_DANCER, RAND_INF_FIRE_TEMPLE_MQ_WONDER_AFTER_FLARE_DANCER },
    { RC_FIRE_TEMPLE_MQ_WONDER_STAIRCASE, RAND_INF_FIRE_TEMPLE_MQ_WONDER_STAIRCASE },
    { RC_WATER_TEMPLE_MQ_WONDER_LIZALFOS_ROOM, RAND_INF_WATER_TEMPLE_MQ_WONDER_LIZALFOS_ROOM },
    { RC_WATER_TEMPLE_MQ_WONDER_LONGSHOT_ROOM, RAND_INF_WATER_TEMPLE_MQ_WONDER_LONGSHOT_ROOM },
    { RC_WATER_TEMPLE_MQ_WONDER_STALFOS_ROOM, RAND_INF_WATER_TEMPLE_MQ_WONDER_STALFOS_ROOM },
    { RC_WATER_TEMPLE_MQ_WONDER_HOOKSHOT_STAIRCASE_RIGHT_1,
      RAND_INF_WATER_TEMPLE_MQ_WONDER_HOOKSHOT_STAIRCASE_RIGHT_1 },
    { RC_WATER_TEMPLE_MQ_WONDER_HOOKSHOT_STAIRCASE_RIGHT_2,
      RAND_INF_WATER_TEMPLE_MQ_WONDER_HOOKSHOT_STAIRCASE_RIGHT_2 },
    { RC_WATER_TEMPLE_MQ_WONDER_HOOKSHOT_STAIRCASE_RIGHT_3,
      RAND_INF_WATER_TEMPLE_MQ_WONDER_HOOKSHOT_STAIRCASE_RIGHT_3 },
    { RC_WATER_TEMPLE_MQ_WONDER_HOOKSHOT_STAIRCASE_LEFT_1, RAND_INF_WATER_TEMPLE_MQ_WONDER_HOOKSHOT_STAIRCASE_LEFT_1 },
    { RC_WATER_TEMPLE_MQ_WONDER_HOOKSHOT_STAIRCASE_LEFT_2, RAND_INF_WATER_TEMPLE_MQ_WONDER_HOOKSHOT_STAIRCASE_LEFT_2 },
    { RC_WATER_TEMPLE_MQ_WONDER_HOOKSHOT_STAIRCASE_LEFT_3, RAND_INF_WATER_TEMPLE_MQ_WONDER_HOOKSHOT_STAIRCASE_LEFT_3 },
    { RC_WATER_TEMPLE_MQ_WONDER_AFTER_DARK_LINK, RAND_INF_WATER_TEMPLE_MQ_WONDER_AFTER_DARK_LINK },
    { RC_WATER_TEMPLE_MQ_WONDER_DRAGON_ROOM_LEFT_EYE, RAND_INF_WATER_TEMPLE_MQ_WONDER_DRAGON_ROOM_LEFT_EYE },
    { RC_WATER_TEMPLE_MQ_WONDER_DRAGON_ROOM_RIGHT_EYE, RAND_INF_WATER_TEMPLE_MQ_WONDER_DRAGON_ROOM_RIGHT_EYE },
    { RC_WATER_TEMPLE_MQ_WONDER_DRAGON_ROOM_PORTRAIT, RAND_INF_WATER_TEMPLE_MQ_WONDER_DRAGON_ROOM_PORTRAIT },
    { RC_WATER_TEMPLE_MQ_WONDER_TRIPLE_TORCHES, RAND_INF_WATER_TEMPLE_MQ_WONDER_TRIPLE_TORCHES },
    { RC_WATER_TEMPLE_MQ_WONDER_WATER_SPROUTS_1, RAND_INF_WATER_TEMPLE_MQ_WONDER_WATER_SPROUTS_1 },
    { RC_WATER_TEMPLE_MQ_WONDER_WATER_SPROUTS_2, RAND_INF_WATER_TEMPLE_MQ_WONDER_WATER_SPROUTS_2 },
    { RC_WATER_TEMPLE_MQ_WONDER_FREESTANDING_ROOM, RAND_INF_WATER_TEMPLE_MQ_WONDER_FREESTANDING_ROOM },
    { RC_WATER_TEMPLE_MQ_WONDER_BEFORE_BOSS_1, RAND_INF_WATER_TEMPLE_MQ_WONDER_BEFORE_BOSS_1 },
    { RC_WATER_TEMPLE_MQ_WONDER_BEFORE_BOSS_2, RAND_INF_WATER_TEMPLE_MQ_WONDER_BEFORE_BOSS_2 },
    { RC_WATER_TEMPLE_MQ_WONDER_UNDER_PILLAR_ROOM, RAND_INF_WATER_TEMPLE_MQ_WONDER_UNDER_PILLAR_ROOM },
    { RC_WATER_TEMPLE_MQ_WONDER_LIZALFOS_HALLWAY, RAND_INF_WATER_TEMPLE_MQ_WONDER_LIZALFOS_HALLWAY },
    { RC_WATER_TEMPLE_MQ_WONDER_GS_STORAGE_ROOM, RAND_INF_WATER_TEMPLE_MQ_WONDER_GS_STORAGE_ROOM },
    { RC_SPIRIT_TEMPLE_MQ_WONDER_CHEST_HAMMER, RAND_INF_SPIRIT_TEMPLE_MQ_WONDER_CHEST_HAMMER },
    { RC_SPIRIT_TEMPLE_MQ_WONDER_CHEST_SLASH, RAND_INF_SPIRIT_TEMPLE_MQ_WONDER_CHEST_SLASH },
    { RC_SHADOW_TEMPLE_MQ_WONDER_THREE_POTS, RAND_INF_SHADOW_TEMPLE_MQ_WONDER_THREE_POTS },
    { RC_BOTTOM_OF_THE_WELL_MQ_WONDER_MAIN_ROOM_LEFT_1, RAND_INF_BOTTOM_OF_THE_WELL_MQ_WONDER_MAIN_ROOM_LEFT_1 },
    { RC_BOTTOM_OF_THE_WELL_MQ_WONDER_MAIN_ROOM_LEFT_2, RAND_INF_BOTTOM_OF_THE_WELL_MQ_WONDER_MAIN_ROOM_LEFT_2 },
    { RC_BOTTOM_OF_THE_WELL_MQ_WONDER_MAIN_ROOM_LEFT_3, RAND_INF_BOTTOM_OF_THE_WELL_MQ_WONDER_MAIN_ROOM_LEFT_3 },
    { RC_BOTTOM_OF_THE_WELL_MQ_WONDER_MAIN_ROOM_LEFT_4, RAND_INF_BOTTOM_OF_THE_WELL_MQ_WONDER_MAIN_ROOM_LEFT_4 },
    { RC_BOTTOM_OF_THE_WELL_MQ_WONDER_MAIN_ROOM_RIGHT_1, RAND_INF_BOTTOM_OF_THE_WELL_MQ_WONDER_MAIN_ROOM_RIGHT_1 },
    { RC_BOTTOM_OF_THE_WELL_MQ_WONDER_MAIN_ROOM_RIGHT_2, RAND_INF_BOTTOM_OF_THE_WELL_MQ_WONDER_MAIN_ROOM_RIGHT_2 },
    { RC_BOTTOM_OF_THE_WELL_MQ_WONDER_MAIN_ROOM_RIGHT_3, RAND_INF_BOTTOM_OF_THE_WELL_MQ_WONDER_MAIN_ROOM_RIGHT_3 },
    { RC_BOTTOM_OF_THE_WELL_MQ_WONDER_MAIN_ROOM_RIGHT_4, RAND_INF_BOTTOM_OF_THE_WELL_MQ_WONDER_MAIN_ROOM_RIGHT_4 },
    { RC_BOTTOM_OF_THE_WELL_MQ_WONDER_SIDE_ROOM_1, RAND_INF_BOTTOM_OF_THE_WELL_MQ_WONDER_SIDE_ROOM_1 },
    { RC_BOTTOM_OF_THE_WELL_MQ_WONDER_SIDE_ROOM_2, RAND_INF_BOTTOM_OF_THE_WELL_MQ_WONDER_SIDE_ROOM_2 },
    { RC_BOTTOM_OF_THE_WELL_MQ_WONDER_SIDE_ROOM_3, RAND_INF_BOTTOM_OF_THE_WELL_MQ_WONDER_SIDE_ROOM_3 },
    { RC_BOTTOM_OF_THE_WELL_MQ_WONDER_SIDE_ROOM_4, RAND_INF_BOTTOM_OF_THE_WELL_MQ_WONDER_SIDE_ROOM_4 },
    { RC_GERUDO_TRAINING_GROUND_MQ_WONDER_DINOLFOS_ROOM, RAND_INF_GERUDO_TRAINING_GROUND_MQ_WONDER_DINOLFOS_ROOM },
    { RC_GERUDO_TRAINING_GROUND_MQ_WONDER_EYE_STATUE, RAND_INF_GERUDO_TRAINING_GROUND_MQ_WONDER_EYE_STATUE },
    { RC_GANONS_CASTLE_MQ_WONDER_SHADOW_TRIAL, RAND_INF_GANONS_CASTLE_MQ_WONDER_SHADOW_TRIAL },
    // Beggar
    { RC_MK_BEGGAR_BUGS, RAND_INF_MK_BEGGAR_BUGS },
    { RC_MK_BEGGAR_FISH, RAND_INF_MK_BEGGAR_FISH },
    { RC_MK_BEGGAR_BLUE_FIRE, RAND_INF_MK_BEGGAR_BLUE_FIRE },
    { RC_KAK_BEGGAR_BUGS, RAND_INF_KAK_BEGGAR_BUGS },
    { RC_KAK_BEGGAR_FISH, RAND_INF_KAK_BEGGAR_FISH },
    { RC_KAK_BEGGAR_BLUE_FIRE, RAND_INF_KAK_BEGGAR_BLUE_FIRE },
    // Icicles
    { RC_ICE_CAVERN_ENTRANCE_LEFT_STALAGMITE, RAND_INF_ICE_CAVERN_ENTRANCE_LEFT_STALAGMITE },
    { RC_ICE_CAVERN_ENTRANCE_MIDDLE_STALAGMITE, RAND_INF_ICE_CAVERN_ENTRANCE_MIDDLE_STALAGMITE },
    { RC_ICE_CAVERN_ENTRANCE_RIGHT_STALAGMITE, RAND_INF_ICE_CAVERN_ENTRANCE_RIGHT_STALAGMITE },
    { RC_ICE_CAVERN_ENTRANCE_STALACTITE_1, RAND_INF_ICE_CAVERN_ENTRANCE_STALACTITE_1 },
    { RC_ICE_CAVERN_ENTRANCE_STALACTITE_2, RAND_INF_ICE_CAVERN_ENTRANCE_STALACTITE_2 },
    { RC_ICE_CAVERN_LOBBY_STALACTITE, RAND_INF_ICE_CAVERN_LOBBY_STALACTITE },
    { RC_ICE_CAVERN_LOBBY_LEFT_STALAGMITE, RAND_INF_ICE_CAVERN_LOBBY_LEFT_STALAGMITE },
    { RC_ICE_CAVERN_LOBBY_MIDDLE_STALAGMITE, RAND_INF_ICE_CAVERN_LOBBY_MIDDLE_STALAGMITE },
    { RC_ICE_CAVERN_LOBBY_RIGHT_STALAGMITE, RAND_INF_ICE_CAVERN_LOBBY_RIGHT_STALAGMITE },
    { RC_ICE_CAVERN_AFTER_LOBBY_STALACTITE, RAND_INF_ICE_CAVERN_AFTER_LOBBY_STALACTITE },
    { RC_ICE_CAVERN_AFTER_LOBBY_LEFT_STALAGMITE, RAND_INF_ICE_CAVERN_AFTER_LOBBY_LEFT_STALAGMITE },
    { RC_ICE_CAVERN_AFTER_LOBBY_CENTER_LEFT_STALAGMITE, RAND_INF_ICE_CAVERN_AFTER_LOBBY_CENTER_LEFT_STALAGMITE },
    { RC_ICE_CAVERN_AFTER_LOBBY_CENTER_RIGHT_STALAGMITE, RAND_INF_ICE_CAVERN_AFTER_LOBBY_CENTER_RIGHT_STALAGMITE },
    { RC_ICE_CAVERN_AFTER_LOBBY_RIGHT_STALAGMITE, RAND_INF_ICE_CAVERN_AFTER_LOBBY_RIGHT_STALAGMITE },
    { RC_ICE_CAVERN_SPINNING_BLADE_LEFT_STALAGMITE, RAND_INF_ICE_CAVERN_SPINNING_BLADE_LEFT_STALAGMITE },
    { RC_ICE_CAVERN_SPINNING_BLADE_MIDDLE_STALAGMITE, RAND_INF_ICE_CAVERN_SPINNING_BLADE_MIDDLE_STALAGMITE },
    { RC_ICE_CAVERN_SPINNING_BLADE_RIGHT_STALAGMITE, RAND_INF_ICE_CAVERN_SPINNING_BLADE_RIGHT_STALAGMITE },
    { RC_ICE_CAVERN_MAP_HALLWAY_STALACTITE_1, RAND_INF_ICE_CAVERN_MAP_HALLWAY_STALACTITE_1 },
    { RC_ICE_CAVERN_MAP_HALLWAY_STALACTITE_2, RAND_INF_ICE_CAVERN_MAP_HALLWAY_STALACTITE_2 },
    { RC_ICE_CAVERN_MAP_HALLWAY_STALACTITE_3, RAND_INF_ICE_CAVERN_MAP_HALLWAY_STALACTITE_3 },
    { RC_ICE_CAVERN_MAP_HALLWAY_STALACTITE_4, RAND_INF_ICE_CAVERN_MAP_HALLWAY_STALACTITE_4 },
    { RC_ICE_CAVERN_MAP_HALLWAY_STALACTITE_5, RAND_INF_ICE_CAVERN_MAP_HALLWAY_STALACTITE_5 },
    { RC_ICE_CAVERN_MAP_HALLWAY_LEFT_STALAGMITE, RAND_INF_ICE_CAVERN_MAP_HALLWAY_LEFT_STALAGMITE },
    { RC_ICE_CAVERN_MAP_HALLWAY_MIDDLE_STALAGMITE, RAND_INF_ICE_CAVERN_MAP_HALLWAY_MIDDLE_STALAGMITE },
    { RC_ICE_CAVERN_MAP_HALLWAY_RIGHT_STALAGMITE, RAND_INF_ICE_CAVERN_MAP_HALLWAY_RIGHT_STALAGMITE },
    { RC_ICE_CAVERN_HEART_PIECE_ROOM_CENTER_STALACTITE_1, RAND_INF_ICE_CAVERN_HEART_PIECE_ROOM_CENTER_STALACTITE_1 },
    { RC_ICE_CAVERN_HEART_PIECE_ROOM_CENTER_STALACTITE_2, RAND_INF_ICE_CAVERN_HEART_PIECE_ROOM_CENTER_STALACTITE_2 },
    { RC_ICE_CAVERN_HEART_PIECE_ROOM_CENTER_STALACTITE_3, RAND_INF_ICE_CAVERN_HEART_PIECE_ROOM_CENTER_STALACTITE_3 },
    { RC_ICE_CAVERN_HEART_PIECE_ROOM_CENTER_STALACTITE_4, RAND_INF_ICE_CAVERN_HEART_PIECE_ROOM_CENTER_STALACTITE_4 },
    { RC_ICE_CAVERN_HEART_PIECE_ROOM_CENTER_STALACTITE_5, RAND_INF_ICE_CAVERN_HEART_PIECE_ROOM_CENTER_STALACTITE_5 },
    { RC_ICE_CAVERN_HEART_PIECE_ROOM_LEFT_STALACTITE_1, RAND_INF_ICE_CAVERN_HEART_PIECE_ROOM_LEFT_STALACTITE_1 },
    { RC_ICE_CAVERN_HEART_PIECE_ROOM_LEFT_STALACTITE_2, RAND_INF_ICE_CAVERN_HEART_PIECE_ROOM_LEFT_STALACTITE_2 },
    { RC_ICE_CAVERN_HEART_PIECE_ROOM_CENTER_STALAGMITE_1, RAND_INF_ICE_CAVERN_HEART_PIECE_ROOM_CENTER_STALAGMITE_1 },
    { RC_ICE_CAVERN_HEART_PIECE_ROOM_CENTER_STALAGMITE_2, RAND_INF_ICE_CAVERN_HEART_PIECE_ROOM_CENTER_STALAGMITE_2 },
    { RC_ICE_CAVERN_HEART_PIECE_ROOM_CENTER_STALAGMITE_3, RAND_INF_ICE_CAVERN_HEART_PIECE_ROOM_CENTER_STALAGMITE_3 },
    { RC_ICE_CAVERN_HEART_PIECE_ROOM_CENTER_STALAGMITE_4, RAND_INF_ICE_CAVERN_HEART_PIECE_ROOM_CENTER_STALAGMITE_4 },
    { RC_ICE_CAVERN_HEART_PIECE_ROOM_CENTER_STALAGMITE_5, RAND_INF_ICE_CAVERN_HEART_PIECE_ROOM_CENTER_STALAGMITE_5 },
    { RC_ICE_CAVERN_HEART_PIECE_ROOM_CENTER_STALAGMITE_6, RAND_INF_ICE_CAVERN_HEART_PIECE_ROOM_CENTER_STALAGMITE_6 },
    { RC_ICE_CAVERN_HEART_PIECE_ROOM_LEFT_STALAGMITE_1, RAND_INF_ICE_CAVERN_HEART_PIECE_ROOM_LEFT_STALAGMITE_1 },
    { RC_ICE_CAVERN_HEART_PIECE_ROOM_LEFT_STALAGMITE_2, RAND_INF_ICE_CAVERN_HEART_PIECE_ROOM_LEFT_STALAGMITE_2 },
    { RC_ICE_CAVERN_HEART_PIECE_ROOM_LEFT_STALAGMITE_3, RAND_INF_ICE_CAVERN_HEART_PIECE_ROOM_LEFT_STALAGMITE_3 },
    { RC_ICE_CAVERN_HEART_PIECE_ROOM_RIGHT_STALAGMITE_1, RAND_INF_ICE_CAVERN_HEART_PIECE_ROOM_RIGHT_STALAGMITE_1 },
    { RC_ICE_CAVERN_HEART_PIECE_ROOM_RIGHT_STALAGMITE_2, RAND_INF_ICE_CAVERN_HEART_PIECE_ROOM_RIGHT_STALAGMITE_2 },
    { RC_ICE_CAVERN_HEART_PIECE_ROOM_RIGHT_STALAGMITE_3, RAND_INF_ICE_CAVERN_HEART_PIECE_ROOM_RIGHT_STALAGMITE_3 },
    { RC_ICE_CAVERN_HEART_PIECE_ROOM_RIGHT_STALAGMITE_4, RAND_INF_ICE_CAVERN_HEART_PIECE_ROOM_RIGHT_STALAGMITE_4 },
    { RC_ICE_CAVERN_PUSH_BLOCK_HALL_LEFT_STALAGMITE, RAND_INF_ICE_CAVERN_PUSH_BLOCK_HALL_LEFT_STALAGMITE },
    { RC_ICE_CAVERN_PUSH_BLOCK_HALL_CENTER_LEFT_STALAGMITE,
      RAND_INF_ICE_CAVERN_PUSH_BLOCK_HALL_CENTER_LEFT_STALAGMITE },
    { RC_ICE_CAVERN_PUSH_BLOCK_HALL_CENTER_STALAGMITE, RAND_INF_ICE_CAVERN_PUSH_BLOCK_HALL_CENTER_STALAGMITE },
    { RC_ICE_CAVERN_PUSH_BLOCK_HALL_CENTER_RIGHT_STALAGMITE,
      RAND_INF_ICE_CAVERN_PUSH_BLOCK_HALL_CENTER_RIGHT_STALAGMITE },
    { RC_ICE_CAVERN_PUSH_BLOCK_HALL_RIGHT_STALAGMITE, RAND_INF_ICE_CAVERN_PUSH_BLOCK_HALL_RIGHT_STALAGMITE },
    { RC_ICE_CAVERN_PUSH_BLOCK_HALL_STALACTITE_1, RAND_INF_ICE_CAVERN_PUSH_BLOCK_HALL_STALACTITE_1 },
    { RC_ICE_CAVERN_PUSH_BLOCK_HALL_STALACTITE_2, RAND_INF_ICE_CAVERN_PUSH_BLOCK_HALL_STALACTITE_2 },
    { RC_ICE_CAVERN_PUSH_BLOCK_HALL_STALACTITE_3, RAND_INF_ICE_CAVERN_PUSH_BLOCK_HALL_STALACTITE_3 },
    { RC_ICE_CAVERN_NEAR_END_STALACTITE_1, RAND_INF_ICE_CAVERN_NEAR_END_STALACTITE_1 },
    { RC_ICE_CAVERN_NEAR_END_STALACTITE_2, RAND_INF_ICE_CAVERN_NEAR_END_STALACTITE_2 },
    { RC_ICE_CAVERN_NEAR_END_STALACTITE_3, RAND_INF_ICE_CAVERN_NEAR_END_STALACTITE_3 },
    { RC_ICE_CAVERN_NEAR_END_STALACTITE_4, RAND_INF_ICE_CAVERN_NEAR_END_STALACTITE_4 },
    { RC_ICE_CAVERN_NEAR_END_STALAGMITE_1, RAND_INF_ICE_CAVERN_NEAR_END_STALAGMITE_1 },
    { RC_ICE_CAVERN_NEAR_END_STALAGMITE_2, RAND_INF_ICE_CAVERN_NEAR_END_STALAGMITE_2 },
    { RC_ICE_CAVERN_NEAR_END_STALAGMITE_3, RAND_INF_ICE_CAVERN_NEAR_END_STALAGMITE_3 },
    { RC_ICE_CAVERN_NEAR_END_STALAGMITE_4, RAND_INF_ICE_CAVERN_NEAR_END_STALAGMITE_4 },
    { RC_ICE_CAVERN_NEAR_END_STALAGMITE_5, RAND_INF_ICE_CAVERN_NEAR_END_STALAGMITE_5 },
    { RC_ICE_CAVERN_NEAR_END_STALAGMITE_6, RAND_INF_ICE_CAVERN_NEAR_END_STALAGMITE_6 },
    { RC_ICE_CAVERN_NEAR_END_STALAGMITE_7, RAND_INF_ICE_CAVERN_NEAR_END_STALAGMITE_7 },
    { RC_ICE_CAVERN_NEAR_END_STALAGMITE_8, RAND_INF_ICE_CAVERN_NEAR_END_STALAGMITE_8 },
    { RC_GANONS_CASTLE_WATER_TRIAL_STALAGMITE_1, RAND_INF_GANONS_CASTLE_WATER_TRIAL_STALAGMITE_1 },
    { RC_GANONS_CASTLE_WATER_TRIAL_STALAGMITE_2, RAND_INF_GANONS_CASTLE_WATER_TRIAL_STALAGMITE_2 },
    { RC_GANONS_CASTLE_WATER_TRIAL_STALAGMITE_3, RAND_INF_GANONS_CASTLE_WATER_TRIAL_STALAGMITE_3 },
    { RC_GANONS_CASTLE_WATER_TRIAL_STALAGMITE_4, RAND_INF_GANONS_CASTLE_WATER_TRIAL_STALAGMITE_4 },
    { RC_GANONS_CASTLE_WATER_TRIAL_STALAGMITE_5, RAND_INF_GANONS_CASTLE_WATER_TRIAL_STALAGMITE_5 },
    { RC_GANONS_CASTLE_WATER_TRIAL_STALAGMITE_6, RAND_INF_GANONS_CASTLE_WATER_TRIAL_STALAGMITE_6 },
    { RC_GANONS_CASTLE_WATER_TRIAL_STALAGMITE_7, RAND_INF_GANONS_CASTLE_WATER_TRIAL_STALAGMITE_7 },
    { RC_GANONS_CASTLE_WATER_TRIAL_STALAGMITE_8, RAND_INF_GANONS_CASTLE_WATER_TRIAL_STALAGMITE_8 },
    { RC_GANONS_CASTLE_WATER_TRIAL_STALAGMITE_9, RAND_INF_GANONS_CASTLE_WATER_TRIAL_STALAGMITE_9 },
    { RC_GANONS_CASTLE_WATER_TRIAL_STALAGMITE_10, RAND_INF_GANONS_CASTLE_WATER_TRIAL_STALAGMITE_10 },
    { RC_GANONS_CASTLE_WATER_TRIAL_LEFT_STALAGMITE_1, RAND_INF_GANONS_CASTLE_WATER_TRIAL_LEFT_STALAGMITE_1 },
    { RC_GANONS_CASTLE_WATER_TRIAL_LEFT_STALAGMITE_2, RAND_INF_GANONS_CASTLE_WATER_TRIAL_LEFT_STALAGMITE_2 },
    { RC_GANONS_CASTLE_WATER_TRIAL_RIGHT_STALAGMITE_1, RAND_INF_GANONS_CASTLE_WATER_TRIAL_RIGHT_STALAGMITE_1 },
    { RC_GANONS_CASTLE_WATER_TRIAL_RIGHT_STALAGMITE_2, RAND_INF_GANONS_CASTLE_WATER_TRIAL_RIGHT_STALAGMITE_2 },
    { RC_GANONS_CASTLE_WATER_TRIAL_STALACTITE_1, RAND_INF_GANONS_CASTLE_WATER_TRIAL_STALACTITE_1 },
    { RC_GANONS_CASTLE_WATER_TRIAL_STALACTITE_2, RAND_INF_GANONS_CASTLE_WATER_TRIAL_STALACTITE_2 },
    { RC_GANONS_CASTLE_WATER_TRIAL_STALACTITE_3, RAND_INF_GANONS_CASTLE_WATER_TRIAL_STALACTITE_3 },
    { RC_GANONS_CASTLE_WATER_TRIAL_STALACTITE_4, RAND_INF_GANONS_CASTLE_WATER_TRIAL_STALACTITE_4 },
    { RC_GANONS_CASTLE_WATER_TRIAL_STALACTITE_5, RAND_INF_GANONS_CASTLE_WATER_TRIAL_STALACTITE_5 },
    { RC_GANONS_CASTLE_WATER_TRIAL_STALACTITE_6, RAND_INF_GANONS_CASTLE_WATER_TRIAL_STALACTITE_6 },
    { RC_GANONS_CASTLE_WATER_TRIAL_STALACTITE_7, RAND_INF_GANONS_CASTLE_WATER_TRIAL_STALACTITE_7 },
    { RC_GANONS_CASTLE_WATER_TRIAL_STALACTITE_8, RAND_INF_GANONS_CASTLE_WATER_TRIAL_STALACTITE_8 },
    { RC_GANONS_CASTLE_WATER_TRIAL_STALACTITE_9, RAND_INF_GANONS_CASTLE_WATER_TRIAL_STALACTITE_9 },
    { RC_GANONS_CASTLE_WATER_TRIAL_STALACTITE_10, RAND_INF_GANONS_CASTLE_WATER_TRIAL_STALACTITE_10 },
    { RC_GANONS_CASTLE_WATER_TRIAL_STALACTITE_11, RAND_INF_GANONS_CASTLE_WATER_TRIAL_STALACTITE_11 },
    { RC_ICE_CAVERN_MQ_ENTRANCE_STALAGMITE_1, RAND_INF_ICE_CAVERN_MQ_ENTRANCE_STALAGMITE_1 },
    { RC_ICE_CAVERN_MQ_ENTRANCE_STALAGMITE_2, RAND_INF_ICE_CAVERN_MQ_ENTRANCE_STALAGMITE_2 },
    { RC_ICE_CAVERN_MQ_LOBBY_STALACTITE_1, RAND_INF_ICE_CAVERN_MQ_LOBBY_STALACTITE_1 },
    { RC_ICE_CAVERN_MQ_LOBBY_STALACTITE_2, RAND_INF_ICE_CAVERN_MQ_LOBBY_STALACTITE_2 },
    { RC_ICE_CAVERN_MQ_AFTER_LOBBY_STALAGMITE_1, RAND_INF_ICE_CAVERN_MQ_AFTER_LOBBY_STALAGMITE_1 },
    { RC_ICE_CAVERN_MQ_AFTER_LOBBY_STALAGMITE_2, RAND_INF_ICE_CAVERN_MQ_AFTER_LOBBY_STALAGMITE_2 },
    { RC_ICE_CAVERN_MQ_AFTER_LOBBY_STALAGMITE_3, RAND_INF_ICE_CAVERN_MQ_AFTER_LOBBY_STALAGMITE_3 },
    { RC_ICE_CAVERN_MQ_AFTER_LOBBY_STALAGMITE_4, RAND_INF_ICE_CAVERN_MQ_AFTER_LOBBY_STALAGMITE_4 },
    { RC_ICE_CAVERN_MQ_AFTER_LOBBY_STALAGMITE_5, RAND_INF_ICE_CAVERN_MQ_AFTER_LOBBY_STALAGMITE_5 },
    { RC_ICE_CAVERN_MQ_HUB_STALAGMITE_1, RAND_INF_ICE_CAVERN_MQ_HUB_STALAGMITE_1 },
    { RC_ICE_CAVERN_MQ_HUB_STALAGMITE_2, RAND_INF_ICE_CAVERN_MQ_HUB_STALAGMITE_2 },
    { RC_ICE_CAVERN_MQ_HUB_STALAGMITE_3, RAND_INF_ICE_CAVERN_MQ_HUB_STALAGMITE_3 },
    { RC_ICE_CAVERN_MQ_HUB_STALAGMITE_4, RAND_INF_ICE_CAVERN_MQ_HUB_STALAGMITE_4 },
    { RC_ICE_CAVERN_MQ_HUB_STALAGMITE_5, RAND_INF_ICE_CAVERN_MQ_HUB_STALAGMITE_5 },
    { RC_ICE_CAVERN_MQ_MAP_ROOM_LEFT_STALAGMITE_1, RAND_INF_ICE_CAVERN_MQ_MAP_ROOM_LEFT_STALAGMITE_1 },
    { RC_ICE_CAVERN_MQ_MAP_ROOM_LEFT_STALAGMITE_2, RAND_INF_ICE_CAVERN_MQ_MAP_ROOM_LEFT_STALAGMITE_2 },
    { RC_ICE_CAVERN_MQ_MAP_ROOM_CENTER_STALAGMITE_1, RAND_INF_ICE_CAVERN_MQ_MAP_ROOM_CENTER_STALAGMITE_1 },
    { RC_ICE_CAVERN_MQ_MAP_ROOM_CENTER_STALAGMITE_2, RAND_INF_ICE_CAVERN_MQ_MAP_ROOM_CENTER_STALAGMITE_2 },
    { RC_ICE_CAVERN_MQ_MAP_ROOM_CENTER_STALAGMITE_3, RAND_INF_ICE_CAVERN_MQ_MAP_ROOM_CENTER_STALAGMITE_3 },
    { RC_ICE_CAVERN_MQ_MAP_ROOM_CENTER_STALAGMITE_4, RAND_INF_ICE_CAVERN_MQ_MAP_ROOM_CENTER_STALAGMITE_4 },
    { RC_ICE_CAVERN_MQ_MAP_ROOM_CENTER_STALAGMITE_5, RAND_INF_ICE_CAVERN_MQ_MAP_ROOM_CENTER_STALAGMITE_5 },
    { RC_ICE_CAVERN_MQ_MAP_ROOM_CENTER_STALAGMITE_6, RAND_INF_ICE_CAVERN_MQ_MAP_ROOM_CENTER_STALAGMITE_6 },
    { RC_ICE_CAVERN_MQ_MAP_ROOM_CENTER_STALAGMITE_7, RAND_INF_ICE_CAVERN_MQ_MAP_ROOM_CENTER_STALAGMITE_7 },
    { RC_ICE_CAVERN_MQ_COMPASS_LEFT_STALAGMITE_1, RAND_INF_ICE_CAVERN_MQ_COMPASS_LEFT_STALAGMITE_1 },
    { RC_ICE_CAVERN_MQ_COMPASS_LEFT_STALAGMITE_2, RAND_INF_ICE_CAVERN_MQ_COMPASS_LEFT_STALAGMITE_2 },
    { RC_ICE_CAVERN_MQ_COMPASS_RIGHT_STALAGMITE_1, RAND_INF_ICE_CAVERN_MQ_COMPASS_RIGHT_STALAGMITE_1 },
    { RC_ICE_CAVERN_MQ_COMPASS_RIGHT_STALAGMITE_2, RAND_INF_ICE_CAVERN_MQ_COMPASS_RIGHT_STALAGMITE_2 },
    { RC_ICE_CAVERN_MQ_BEFORE_SCARECROW_STALAGMITE, RAND_INF_ICE_CAVERN_MQ_BEFORE_SCARECROW_STALAGMITE },
    { RC_ICE_CAVERN_MQ_SCARECROW_ROOM_STALACTITE, RAND_INF_ICE_CAVERN_MQ_SCARECROW_ROOM_STALACTITE },
    { RC_ICE_CAVERN_MQ_WEST_CORRIDOR_STALACTITE_1, RAND_INF_ICE_CAVERN_MQ_WEST_CORRIDOR_STALACTITE_1 },
    { RC_ICE_CAVERN_MQ_WEST_CORRIDOR_STALACTITE_2, RAND_INF_ICE_CAVERN_MQ_WEST_CORRIDOR_STALACTITE_2 },
    { RC_ICE_CAVERN_MQ_WEST_CORRIDOR_STALACTITE_3, RAND_INF_ICE_CAVERN_MQ_WEST_CORRIDOR_STALACTITE_3 },
    { RC_GERUDO_TRAINING_GROUND_MQ_BOULDER_ROOM_STALAGMITE_1,
      RAND_INF_GERUDO_TRAINING_GROUND_MQ_BOULDER_ROOM_STALAGMITE_1 },
    { RC_GERUDO_TRAINING_GROUND_MQ_BOULDER_ROOM_STALAGMITE_2,
      RAND_INF_GERUDO_TRAINING_GROUND_MQ_BOULDER_ROOM_STALAGMITE_2 },
    { RC_GERUDO_TRAINING_GROUND_MQ_BOULDER_ROOM_STALAGMITE_3,
      RAND_INF_GERUDO_TRAINING_GROUND_MQ_BOULDER_ROOM_STALAGMITE_3 },
    { RC_GERUDO_TRAINING_GROUND_MQ_BOULDER_ROOM_STALAGMITE_4,
      RAND_INF_GERUDO_TRAINING_GROUND_MQ_BOULDER_ROOM_STALAGMITE_4 },
    { RC_GERUDO_TRAINING_GROUND_MQ_BOULDER_ROOM_STALAGMITE_5,
      RAND_INF_GERUDO_TRAINING_GROUND_MQ_BOULDER_ROOM_STALAGMITE_5 },
    { RC_GERUDO_TRAINING_GROUND_MQ_BOULDER_ROOM_RIGHT_STALACTITE_1,
      RAND_INF_GERUDO_TRAINING_GROUND_MQ_BOULDER_ROOM_RIGHT_STALACTITE_1 },
    { RC_GERUDO_TRAINING_GROUND_MQ_BOULDER_ROOM_RIGHT_STALACTITE_2,
      RAND_INF_GERUDO_TRAINING_GROUND_MQ_BOULDER_ROOM_RIGHT_STALACTITE_2 },
    { RC_GERUDO_TRAINING_GROUND_MQ_BOULDER_ROOM_RIGHT_STALACTITE_3,
      RAND_INF_GERUDO_TRAINING_GROUND_MQ_BOULDER_ROOM_RIGHT_STALACTITE_3 },
    { RC_GERUDO_TRAINING_GROUND_MQ_BOULDER_ROOM_RIGHT_STALACTITE_4,
      RAND_INF_GERUDO_TRAINING_GROUND_MQ_BOULDER_ROOM_RIGHT_STALACTITE_4 },
    { RC_GERUDO_TRAINING_GROUND_MQ_BOULDER_ROOM_LEFT_STALACTITE,
      RAND_INF_GERUDO_TRAINING_GROUND_MQ_BOULDER_ROOM_LEFT_STALACTITE },
    { RC_GERUDO_TRAINING_GROUND_MQ_BOULDER_ROOM_TOP_RIGHT_STALACTITE_1,
      RAND_INF_GERUDO_TRAINING_GROUND_MQ_BOULDER_ROOM_TOP_RIGHT_STALACTITE_1 },
    { RC_GERUDO_TRAINING_GROUND_MQ_BOULDER_ROOM_TOP_RIGHT_STALACTITE_2,
      RAND_INF_GERUDO_TRAINING_GROUND_MQ_BOULDER_ROOM_TOP_RIGHT_STALACTITE_2 },
    { RC_GERUDO_TRAINING_GROUND_MQ_BOULDER_ROOM_TOP_RIGHT_STALACTITE_3,
      RAND_INF_GERUDO_TRAINING_GROUND_MQ_BOULDER_ROOM_TOP_RIGHT_STALACTITE_3 },
    { RC_GANONS_CASTLE_MQ_WATER_TRIAL_LEFT_STALACTITE_1, RAND_INF_GANONS_CASTLE_MQ_WATER_TRIAL_LEFT_STALACTITE_1 },
    { RC_GANONS_CASTLE_MQ_WATER_TRIAL_LEFT_STALACTITE_2, RAND_INF_GANONS_CASTLE_MQ_WATER_TRIAL_LEFT_STALACTITE_2 },
    { RC_GANONS_CASTLE_MQ_WATER_TRIAL_LEFT_STALACTITE_3, RAND_INF_GANONS_CASTLE_MQ_WATER_TRIAL_LEFT_STALACTITE_3 },
    { RC_GANONS_CASTLE_MQ_WATER_TRIAL_RIGHT_STALACTITE_1, RAND_INF_GANONS_CASTLE_MQ_WATER_TRIAL_RIGHT_STALACTITE_1 },
    { RC_GANONS_CASTLE_MQ_WATER_TRIAL_RIGHT_STALACTITE_2, RAND_INF_GANONS_CASTLE_MQ_WATER_TRIAL_RIGHT_STALACTITE_2 },
    { RC_GANONS_CASTLE_MQ_WATER_TRIAL_RIGHT_STALACTITE_3, RAND_INF_GANONS_CASTLE_MQ_WATER_TRIAL_RIGHT_STALACTITE_3 },
    { RC_GANONS_CASTLE_MQ_WATER_TRIAL_LEFT_STALAGMITE_1, RAND_INF_GANONS_CASTLE_MQ_WATER_TRIAL_LEFT_STALAGMITE_1 },
    { RC_GANONS_CASTLE_MQ_WATER_TRIAL_LEFT_STALAGMITE_2, RAND_INF_GANONS_CASTLE_MQ_WATER_TRIAL_LEFT_STALAGMITE_2 },
    { RC_GANONS_CASTLE_MQ_WATER_TRIAL_LEFT_STALAGMITE_3, RAND_INF_GANONS_CASTLE_MQ_WATER_TRIAL_LEFT_STALAGMITE_3 },
    { RC_GANONS_CASTLE_MQ_WATER_TRIAL_LEFT_STALAGMITE_4, RAND_INF_GANONS_CASTLE_MQ_WATER_TRIAL_LEFT_STALAGMITE_4 },
    { RC_GANONS_CASTLE_MQ_WATER_TRIAL_RIGHT_STALAGMITE_1, RAND_INF_GANONS_CASTLE_MQ_WATER_TRIAL_RIGHT_STALAGMITE_1 },
    { RC_GANONS_CASTLE_MQ_WATER_TRIAL_RIGHT_STALAGMITE_2, RAND_INF_GANONS_CASTLE_MQ_WATER_TRIAL_RIGHT_STALAGMITE_2 },
    { RC_GANONS_CASTLE_MQ_WATER_TRIAL_RIGHT_STALAGMITE_3, RAND_INF_GANONS_CASTLE_MQ_WATER_TRIAL_RIGHT_STALAGMITE_3 },
    { RC_GANONS_CASTLE_MQ_WATER_TRIAL_RIGHT_STALAGMITE_4, RAND_INF_GANONS_CASTLE_MQ_WATER_TRIAL_RIGHT_STALAGMITE_4 },
    // Red Ice
    { RC_ZD_KING_ZORA_RED_ICE, RAND_INF_ZD_KING_ZORA_RED_ICE },
    { RC_ZD_ZORA_SHOP_RED_ICE, RAND_INF_ZD_ZORA_SHOP_RED_ICE },
    { RC_ICE_CAVERN_ENTRANCE_RED_ICE, RAND_INF_ICE_CAVERN_ENTRANCE_RED_ICE },
    { RC_ICE_CAVERN_LOBBY_LEFT_RED_ICE, RAND_INF_ICE_CAVERN_LOBBY_LEFT_RED_ICE },
    { RC_ICE_CAVERN_LOBBY_RIGHT_RED_ICE, RAND_INF_ICE_CAVERN_LOBBY_RIGHT_RED_ICE },
    { RC_ICE_CAVERN_SPINNING_BLADE_EAST_RED_ICE, RAND_INF_ICE_CAVERN_SPINNING_BLADE_EAST_RED_ICE },
    { RC_ICE_CAVERN_SPINNING_BLADE_WEST_RED_ICE, RAND_INF_ICE_CAVERN_SPINNING_BLADE_WEST_RED_ICE },
    { RC_ICE_CAVERN_HEART_PIECE_ROOM_FREESTANDING_RED_ICE, RAND_INF_ICE_CAVERN_HEART_PIECE_ROOM_FREESTANDING_RED_ICE },
    { RC_ICE_CAVERN_HEART_PIECE_ROOM_CHEST_RED_ICE, RAND_INF_ICE_CAVERN_HEART_PIECE_ROOM_CHEST_RED_ICE },
    { RC_ICE_CAVERN_MAP_ROOM_POT_RED_ICE, RAND_INF_ICE_CAVERN_MAP_ROOM_POT_RED_ICE },
    { RC_ICE_CAVERN_MAP_ROOM_CHEST_RED_ICE, RAND_INF_ICE_CAVERN_MAP_ROOM_CHEST_RED_ICE },
    { RC_ICE_CAVERN_SILVER_RUPEE_RED_ICE, RAND_INF_ICE_CAVERN_SILVER_RUPEE_RED_ICE },
    { RC_ICE_CAVERN_NEAR_END_LEFT_RED_ICE, RAND_INF_ICE_CAVERN_NEAR_END_LEFT_RED_ICE },
    { RC_ICE_CAVERN_NEAR_END_MIDDLE_RED_ICE, RAND_INF_ICE_CAVERN_NEAR_END_MIDDLE_RED_ICE },
    { RC_ICE_CAVERN_NEAR_END_RIGHT_RED_ICE, RAND_INF_ICE_CAVERN_NEAR_END_RIGHT_RED_ICE },
    { RC_GANONS_CASTLE_WATER_TRIAL_DOOR_RED_ICE, RAND_INF_GANONS_CASTLE_WATER_TRIAL_DOOR_RED_ICE },
    { RC_GANONS_CASTLE_WATER_TRIAL_RUSTED_SWITCH_RED_ICE, RAND_INF_GANONS_CASTLE_WATER_TRIAL_RUSTED_SWITCH_RED_ICE },
    { RC_GERUDO_TRAINING_GROUND_MQ_STALFOS_ROOM_RED_ICE, RAND_INF_GERUDO_TRAINING_GROUND_MQ_STALFOS_ROOM_RED_ICE },
    { RC_ICE_CAVERN_MQ_HUB_WEST_LEFT_RED_ICE, RAND_INF_ICE_CAVERN_MQ_HUB_WEST_LEFT_RED_ICE },
    { RC_ICE_CAVERN_MQ_HUB_WEST_MIDDLE_RED_ICE, RAND_INF_ICE_CAVERN_MQ_HUB_WEST_MIDDLE_RED_ICE },
    { RC_ICE_CAVERN_MQ_HUB_WEST_RIGHT_RED_ICE, RAND_INF_ICE_CAVERN_MQ_HUB_WEST_RIGHT_RED_ICE },
    { RC_ICE_CAVERN_MQ_HUB_LEDGE_LEFT_RED_ICE, RAND_INF_ICE_CAVERN_MQ_HUB_LEDGE_LEFT_RED_ICE },
    { RC_ICE_CAVERN_MQ_HUB_LEDGE_MIDDLE_RED_ICE, RAND_INF_ICE_CAVERN_MQ_HUB_LEDGE_MIDDLE_RED_ICE },
    { RC_ICE_CAVERN_MQ_HUB_LEDGE_RIGHT_RED_ICE, RAND_INF_ICE_CAVERN_MQ_HUB_LEDGE_RIGHT_RED_ICE },
    { RC_ICE_CAVERN_MQ_COMPASS_RED_ICE, RAND_INF_ICE_CAVERN_MQ_COMPASS_RED_ICE },
    { RC_ICE_CAVERN_MQ_MAP_RED_ICE, RAND_INF_ICE_CAVERN_MQ_MAP_RED_ICE },
    { RC_ICE_CAVERN_MQ_SCARECROW_LEFT_RED_ICE, RAND_INF_ICE_CAVERN_MQ_SCARECROW_LEFT_RED_ICE },
    { RC_ICE_CAVERN_MQ_SCARECROW_MIDDLE_RED_ICE, RAND_INF_ICE_CAVERN_MQ_SCARECROW_MIDDLE_RED_ICE },
    { RC_ICE_CAVERN_MQ_SCARECROW_RIGHT_RED_ICE, RAND_INF_ICE_CAVERN_MQ_SCARECROW_RIGHT_RED_ICE },
    { RC_GANONS_CASTLE_MQ_WATER_TRIAL_FIRST_ROOM_LEFT_RED_ICE,
      RAND_INF_GANONS_CASTLE_MQ_WATER_TRIAL_FIRST_ROOM_LEFT_RED_ICE },
    { RC_GANONS_CASTLE_MQ_WATER_TRIAL_FIRST_ROOM_RIGHT_RED_ICE,
      RAND_INF_GANONS_CASTLE_MQ_WATER_TRIAL_FIRST_ROOM_RIGHT_RED_ICE },
    { RC_GANONS_CASTLE_MQ_WATER_TRIAL_FIRST_ROOM_BACK_LEFT_RED_ICE,
      RAND_INF_GANONS_CASTLE_MQ_WATER_TRIAL_FIRST_ROOM_BACK_LEFT_RED_ICE },
    { RC_GANONS_CASTLE_MQ_WATER_TRIAL_FIRST_ROOM_DOOR_RED_ICE_1,
      RAND_INF_GANONS_CASTLE_MQ_WATER_TRIAL_FIRST_ROOM_DOOR_RED_ICE_1 },
    { RC_GANONS_CASTLE_MQ_WATER_TRIAL_FIRST_ROOM_DOOR_RED_ICE_2,
      RAND_INF_GANONS_CASTLE_MQ_WATER_TRIAL_FIRST_ROOM_DOOR_RED_ICE_2 },
    { RC_GANONS_CASTLE_MQ_WATER_TRIAL_FIRST_ROOM_DOOR_RED_ICE_3,
      RAND_INF_GANONS_CASTLE_MQ_WATER_TRIAL_FIRST_ROOM_DOOR_RED_ICE_3 },
    { RC_GANONS_CASTLE_MQ_WATER_TRIAL_FIRST_ROOM_DOOR_RED_ICE_4,
      RAND_INF_GANONS_CASTLE_MQ_WATER_TRIAL_FIRST_ROOM_DOOR_RED_ICE_4 },
    { RC_GANONS_CASTLE_MQ_WATER_TRIAL_SILVER_RUPEE_RED_ICE,
      RAND_INF_GANONS_CASTLE_MQ_WATER_TRIAL_SILVER_RUPEE_RED_ICE },
    { RC_GANONS_CASTLE_MQ_WATER_TRIAL_SECOND_DOOR_RED_ICE_1,
      RAND_INF_GANONS_CASTLE_MQ_WATER_TRIAL_SECOND_DOOR_RED_ICE_1 },
    { RC_GANONS_CASTLE_MQ_WATER_TRIAL_SECOND_DOOR_RED_ICE_2,
      RAND_INF_GANONS_CASTLE_MQ_WATER_TRIAL_SECOND_DOOR_RED_ICE_2 },
    { RC_GANONS_CASTLE_MQ_WATER_TRIAL_SECOND_DOOR_RED_ICE_3,
      RAND_INF_GANONS_CASTLE_MQ_WATER_TRIAL_SECOND_DOOR_RED_ICE_3 },
    { RC_GANONS_CASTLE_MQ_WATER_TRIAL_SECOND_DOOR_RED_ICE_4,
      RAND_INF_GANONS_CASTLE_MQ_WATER_TRIAL_SECOND_DOOR_RED_ICE_4 },
    { RC_GANONS_CASTLE_MQ_WATER_TRIAL_SECOND_DOOR_RED_ICE_5,
      RAND_INF_GANONS_CASTLE_MQ_WATER_TRIAL_SECOND_DOOR_RED_ICE_5 },
};

CheckIdentity Randomizer::IdentifyBeehive(s32 sceneNum, s16 xPosition, s32 respawnData) {
    CheckIdentity beehiveIdentity;

    beehiveIdentity.randomizerInf = RAND_INF_MAX;
    beehiveIdentity.randomizerCheck = RC_UNKNOWN_CHECK;

    if (sceneNum == SCENE_GROTTOS) {
        respawnData = TWO_ACTOR_PARAMS(xPosition, respawnData);
    } else {
        respawnData = TWO_ACTOR_PARAMS(xPosition, 0);
    }

    Rando::Location* location = GetCheckObjectFromActor(ACTOR_OBJ_COMB, sceneNum, respawnData);

    if (location->GetRandomizerCheck() != RC_UNKNOWN_CHECK) {
        beehiveIdentity.randomizerInf = rcToRandomizerInf[location->GetRandomizerCheck()];
        beehiveIdentity.randomizerCheck = location->GetRandomizerCheck();
    }

    return beehiveIdentity;
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

ScrubIdentity Randomizer::IdentifyScrub(s32 sceneNum, s32 actorParams, s32 respawnData) {
    struct ScrubIdentity scrubIdentity;

    scrubIdentity.identity.randomizerInf = RAND_INF_MAX;
    scrubIdentity.identity.randomizerCheck = RC_UNKNOWN_CHECK;
    scrubIdentity.getItemId = GI_NONE;
    scrubIdentity.itemPrice = -1;

    // Scrubs that are 0x06 are loaded as 0x03 when child, switching from selling arrows to seeds
    if (actorParams == 0x06)
        actorParams = 0x03;

    if (sceneNum == SCENE_GROTTOS) {
        actorParams = TWO_ACTOR_PARAMS(actorParams, respawnData);
    }

    Rando::Location* location = GetCheckObjectFromActor(ACTOR_EN_DNS, sceneNum, actorParams);

    if (location->GetRandomizerCheck() != RC_UNKNOWN_CHECK) {
        if (location->GetRandomizerCheck() == RC_HF_DEKU_SCRUB_GROTTO ||
            location->GetRandomizerCheck() == RC_LW_DEKU_SCRUB_GROTTO_FRONT ||
            location->GetRandomizerCheck() == RC_LW_DEKU_SCRUB_NEAR_BRIDGE) {
            if (GetRandoSettingValue(RSK_SHUFFLE_SCRUBS) == RO_SCRUBS_OFF) {
                return scrubIdentity;
            }
        } else if (GetRandoSettingValue(RSK_SHUFFLE_SCRUBS) != RO_SCRUBS_ALL) {
            return scrubIdentity;
        }

        scrubIdentity.identity.randomizerInf = rcToRandomizerInf[location->GetRandomizerCheck()];
        scrubIdentity.identity.randomizerCheck = location->GetRandomizerCheck();
        scrubIdentity.getItemId = (GetItemID)Rando::StaticData::RetrieveItem(location->GetVanillaItem()).GetItemID();
        scrubIdentity.itemPrice =
            OTRGlobals::Instance->gRandoContext->GetItemLocation(scrubIdentity.identity.randomizerCheck)->GetPrice();
    }

    return scrubIdentity;
}

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

CheckIdentity Randomizer::IdentifyCow(s32 sceneNum, s32 posX, s32 posZ) {
    CheckIdentity cowIdentity;

    cowIdentity.randomizerInf = RAND_INF_MAX;
    cowIdentity.randomizerCheck = RC_UNKNOWN_CHECK;

    s32 actorParams = 0x00;
    // Only need to pass params if in a scene with two cows
    if (sceneNum == SCENE_GROTTOS || sceneNum == SCENE_STABLE || sceneNum == SCENE_LON_LON_BUILDINGS) {
        actorParams = TWO_ACTOR_PARAMS(posX, posZ);
    }

    Rando::Location* location = GetCheckObjectFromActor(ACTOR_EN_COW, sceneNum, actorParams);

    if (location->GetRandomizerCheck() != RC_UNKNOWN_CHECK) {
        cowIdentity.randomizerInf = rcToRandomizerInf[location->GetRandomizerCheck()];
        cowIdentity.randomizerCheck = location->GetRandomizerCheck();
    }

    return cowIdentity;
}

CheckIdentity Randomizer::IdentifyPot(s32 sceneNum, s32 posX, s32 posZ) {
    CheckIdentity potIdentity;
    uint32_t potSceneNum = sceneNum;

    if (sceneNum == SCENE_GANONDORF_BOSS) {
        potSceneNum = SCENE_GANONS_TOWER;
    }

    potIdentity.randomizerInf = RAND_INF_MAX;
    potIdentity.randomizerCheck = RC_UNKNOWN_CHECK;

    s32 actorParams = TWO_ACTOR_PARAMS(posX, posZ);

    Rando::Location* location = GetCheckObjectFromActor(ACTOR_OBJ_TSUBO, potSceneNum, actorParams);

    if (location->GetRandomizerCheck() == RC_UNKNOWN_CHECK) {
        LUSLOG_WARN("IdentifyPot did not receive a valid RC value (%d).", location->GetRandomizerCheck());
    } else {
        potIdentity.randomizerInf = rcToRandomizerInf[location->GetRandomizerCheck()];
        potIdentity.randomizerCheck = location->GetRandomizerCheck();
    }

    return potIdentity;
}

CheckIdentity Randomizer::IdentifyFish(s32 sceneNum, s32 actorParams) {
    CheckIdentity fishIdentity;

    fishIdentity.randomizerInf = RAND_INF_MAX;
    fishIdentity.randomizerCheck = RC_UNKNOWN_CHECK;

    // Fishsanity will determine what the identity of the fish should be
    if (sceneNum == SCENE_FISHING_POND) {
        return OTRGlobals::Instance->gRandoContext->GetFishsanity()->IdentifyPondFish(actorParams);
    }

    Rando::Location* location = GetCheckObjectFromActor(ACTOR_EN_FISH, sceneNum, actorParams);

    if (location->GetRandomizerCheck() != RC_UNKNOWN_CHECK) {
        fishIdentity.randomizerInf = rcToRandomizerInf[location->GetRandomizerCheck()];
        fishIdentity.randomizerCheck = location->GetRandomizerCheck();
    }

    return fishIdentity;
}

CheckIdentity Randomizer::IdentifyGrass(s32 sceneNum, s32 posX, s32 posZ, s32 respawnData, s32 linkAge) {
    CheckIdentity grassIdentity;

    grassIdentity.randomizerInf = RAND_INF_MAX;
    grassIdentity.randomizerCheck = RC_UNKNOWN_CHECK;

    if (sceneNum == SCENE_GROTTOS) {
        respawnData = TWO_ACTOR_PARAMS(posX, respawnData);
    } else {
        // We'll just pretend it's always daytime for our market bushes.
        if (sceneNum == SCENE_MARKET_NIGHT) {
            sceneNum = SCENE_MARKET_DAY;

            /*
                The two bushes by the tree are not in the same spot
                between night and day. We'll assume the coordinates
                of the daytime bushes so that we can count them as
                the same locations.
            */
            if (posX == -74) {
                posX = -106;
                posZ = 277;
            }
            if (posX == -87) {
                posX = -131;
                posZ = 225;
            }
        }

        /*
            Same as with Market. ZR has a bush slightly off pos
            between Child and Adult. This is to merge them into
            a single location.
        */
        if (sceneNum == SCENE_ZORAS_RIVER) {
            if (posX == 233) {
                posX = 231;
                posZ = -1478;
            }
        }

        // The two bushes behind the sign in KF should be separate
        // locations between Child and Adult.
        if (sceneNum == SCENE_KOKIRI_FOREST && linkAge == 0) {
            if (posX == -498 || posX == -523) {
                posZ = 0xFF;
            }
        }

        respawnData = TWO_ACTOR_PARAMS(posX, posZ);
    }

    Rando::Location* location = GetCheckObjectFromActor(ACTOR_EN_KUSA, sceneNum, respawnData);

    if (location->GetRandomizerCheck() != RC_UNKNOWN_CHECK) {
        grassIdentity.randomizerInf = rcToRandomizerInf[location->GetRandomizerCheck()];
        grassIdentity.randomizerCheck = location->GetRandomizerCheck();
    }

    return grassIdentity;
}

CheckIdentity Randomizer::IdentifyCrate(s32 sceneNum, s32 posX, s32 posZ) {
    CheckIdentity crateIdentity;
    uint32_t crateSceneNum = sceneNum;

    // pretend night is day to align crates in market and align GF child/adult crates
    if (sceneNum == SCENE_MARKET_NIGHT) {
        crateSceneNum = SCENE_MARKET_DAY;
    } else if (sceneNum == SCENE_GERUDOS_FORTRESS && gPlayState->linkAgeOnLoad == 1 && posX == 310) {
        if (posZ == -1830) {
            posZ = -1842;
        } else if (posZ == -1770) {
            posZ = -1782;
        }
    }

    crateIdentity.randomizerInf = RAND_INF_MAX;
    crateIdentity.randomizerCheck = RC_UNKNOWN_CHECK;

    s32 actorParams = TWO_ACTOR_PARAMS(posX, posZ);

    Rando::Location* location = GetCheckObjectFromActor(ACTOR_OBJ_KIBAKO2, crateSceneNum, actorParams);

    if (location->GetRandomizerCheck() == RC_UNKNOWN_CHECK) {
        LUSLOG_WARN("IdentifyCrate did not receive a valid RC value (%d).", location->GetRandomizerCheck());
        assert(false);
    } else {
        crateIdentity.randomizerInf = rcToRandomizerInf[location->GetRandomizerCheck()];
        crateIdentity.randomizerCheck = location->GetRandomizerCheck();
    }

    return crateIdentity;
}

CheckIdentity Randomizer::IdentifySmallCrate(s32 sceneNum, s32 posX, s32 posZ) {
    CheckIdentity smallCrateIdentity;
    uint32_t smallCrateSceneNum = sceneNum;

    smallCrateIdentity.randomizerInf = RAND_INF_MAX;
    smallCrateIdentity.randomizerCheck = RC_UNKNOWN_CHECK;

    s32 actorParams = TWO_ACTOR_PARAMS(posX, posZ);

    Rando::Location* location = GetCheckObjectFromActor(ACTOR_OBJ_KIBAKO, smallCrateSceneNum, actorParams);

    if (location->GetRandomizerCheck() == RC_UNKNOWN_CHECK) {
        LUSLOG_WARN("IdentifyCrate did not receive a valid RC value (%d).", location->GetRandomizerCheck());
        assert(false);
    } else {
        smallCrateIdentity.randomizerInf = rcToRandomizerInf[location->GetRandomizerCheck()];
        smallCrateIdentity.randomizerCheck = location->GetRandomizerCheck();
    }

    return smallCrateIdentity;
}

CheckIdentity Randomizer::IdentifyRock(s32 sceneNum, s32 posX, s32 posZ) {
    CheckIdentity rockIdentity;

    rockIdentity.randomizerInf = RAND_INF_MAX;
    rockIdentity.randomizerCheck = RC_UNKNOWN_CHECK;

    Rando::Location* location = GetCheckObjectFromActor(ACTOR_EN_ISHI, sceneNum, TWO_ACTOR_PARAMS(posX, posZ));

    if (location->GetRandomizerCheck() != RC_UNKNOWN_CHECK) {
        rockIdentity.randomizerInf = rcToRandomizerInf[location->GetRandomizerCheck()];
        rockIdentity.randomizerCheck = location->GetRandomizerCheck();
    } else {
        LUSLOG_WARN("IdentifyRock did not receive a valid RC value %d,%d.", posX, posZ);
    }

    return rockIdentity;
}

CheckIdentity Randomizer::IdentifyTree(s32 sceneNum, s32 posX, s32 posZ) {
    CheckIdentity treeIdentity;

    if (sceneNum == SCENE_MARKET_NIGHT) {
        sceneNum = SCENE_MARKET_DAY;
    }

    s32 actorParams = TWO_ACTOR_PARAMS(posX, posZ);
    Rando::Location* location = GetCheckObjectFromActor(ACTOR_EN_WOOD02, sceneNum, actorParams);
    if (location->GetRandomizerCheck() != RC_UNKNOWN_CHECK &&
        (location->GetRCType() != RCTYPE_NLTREE || GetRandoSettingValue(RSK_LOGIC_RULES) == RO_LOGIC_NO_LOGIC)) {
        treeIdentity.randomizerInf = rcToRandomizerInf[location->GetRandomizerCheck()];
        treeIdentity.randomizerCheck = location->GetRandomizerCheck();
        return treeIdentity;
    }

    treeIdentity.randomizerInf = RAND_INF_MAX;
    treeIdentity.randomizerCheck = RC_UNKNOWN_CHECK;
    return treeIdentity;
}

CheckIdentity Randomizer::IdentifySign(s32 sceneNum, s32 posX, s32 posZ, s32 id) {
    CheckIdentity signIdentity;
    uint32_t signSceneNum = sceneNum;
    Rando::Location* location = nullptr;

    // align child/adult signs
    if (sceneNum == SCENE_KAKARIKO_VILLAGE && LINK_IS_ADULT && posX == 1165 && posZ == 1545) {
        posZ = 1550;
    } else if (sceneNum == SCENE_GRAVEYARD && LINK_IS_ADULT) {
        if (id == ACTOR_EN_WONDER_TALK2 && posX == -807 && posZ == 266) {
            posX = -805;
        } else if (id == ACTOR_EN_WONDER_TALK) {
            if (posX == 634 && posZ == 260) {
                posX = 654;
                posZ = 258;
            } else if (posX == 634 && posZ == -100) {
                posX = 654;
                posZ = -102;
            } else if (posX == 753 && posZ == 85) {
                posX = 752;
            }
        }
    } else if (sceneNum == SCENE_ZORAS_RIVER && LINK_IS_ADULT && posX == 4097 && posZ == -1399) {
        posX = 4096;
        posZ = -1401;
    }

    signIdentity.randomizerInf = RAND_INF_MAX;
    signIdentity.randomizerCheck = RC_UNKNOWN_CHECK;

    s32 actorParams = TWO_ACTOR_PARAMS(posX, posZ);

    switch (id) {
        case ACTOR_EN_KANBAN:
            location = GetCheckObjectFromActor(ACTOR_EN_KANBAN, signSceneNum, actorParams);
            break;
        case ACTOR_EN_A_OBJ:
            location = GetCheckObjectFromActor(ACTOR_EN_A_OBJ, signSceneNum, actorParams);
            break;
        case ACTOR_EN_WONDER_TALK2:
            location = GetCheckObjectFromActor(ACTOR_EN_WONDER_TALK2, signSceneNum, actorParams);
            break;
        case ACTOR_EN_WONDER_TALK:
            location = GetCheckObjectFromActor(ACTOR_EN_WONDER_TALK, signSceneNum, actorParams);
            break;
        default:
            return signIdentity;
    }

    if (location == nullptr || location->GetRandomizerCheck() == RC_UNKNOWN_CHECK) {
        LUSLOG_WARN("IdentifySign did not receive a valid RC value (%d).", location->GetRandomizerCheck());
    } else {
        signIdentity.randomizerInf = rcToRandomizerInf[location->GetRandomizerCheck()];
        signIdentity.randomizerCheck = location->GetRandomizerCheck();
    }

    return signIdentity;
}

CheckIdentity Randomizer::IdentifyWonderItem(s32 sceneNum, s32 par1, s32 par2) {
    CheckIdentity wonderIdentity;
    uint32_t wonderSceneNum = sceneNum;

    // align oasis trees in colossus between child/adult
    if (sceneNum == SCENE_DESERT_COLOSSUS && LINK_IS_ADULT) {
        if (par1 == 1157 && par2 == 2388) {
            par1 = 1161;
            par2 = 2383;
        } else if (par1 == 1114 && par2 == 2580) {
            par1 = 1113;
            par2 = 2581;
        }
    }

    wonderIdentity.randomizerInf = RAND_INF_MAX;
    wonderIdentity.randomizerCheck = RC_UNKNOWN_CHECK;

    s32 actorParams = TWO_ACTOR_PARAMS(par1, par2);

    Rando::Location* location = GetCheckObjectFromActor(ACTOR_EN_WONDER_ITEM, wonderSceneNum, actorParams);

    if (location->GetRandomizerCheck() == RC_UNKNOWN_CHECK) {
        LUSLOG_WARN("IdentifyWonderItem did not receive a valid RC value (%d).", location->GetRandomizerCheck());
    } else {
        wonderIdentity.randomizerInf = rcToRandomizerInf[location->GetRandomizerCheck()];
        wonderIdentity.randomizerCheck = location->GetRandomizerCheck();
    }

    return wonderIdentity;
}

CheckIdentity Randomizer::IdentifyBeggar(s32 sceneNum, s32 textId) {
    CheckIdentity beggarIdentity;
    beggarIdentity.randomizerInf = RAND_INF_MAX;
    beggarIdentity.randomizerCheck = RC_UNKNOWN_CHECK;

    Rando::Location* location = GetCheckObjectFromActor(ACTOR_EN_HY, sceneNum, textId);
    if (location->GetRandomizerCheck() == RC_UNKNOWN_CHECK) {
        LUSLOG_WARN("IdentifyBeggar did not receive a valid RC value (%d).", location->GetRandomizerCheck());
    } else {
        beggarIdentity.randomizerInf = rcToRandomizerInf[location->GetRandomizerCheck()];
        beggarIdentity.randomizerCheck = location->GetRandomizerCheck();
    }

    return beggarIdentity;
}

CheckIdentity Randomizer::IdentifyIcicle(s32 sceneNum, s32 posX, s32 posZ) {
    struct CheckIdentity icicleIdentity;
    uint32_t icicleSceneNum = sceneNum;

    icicleIdentity.randomizerInf = RAND_INF_MAX;
    icicleIdentity.randomizerCheck = RC_UNKNOWN_CHECK;

    s32 actorParams = TWO_ACTOR_PARAMS(posX, posZ);

    Rando::Location* location = GetCheckObjectFromActor(ACTOR_BG_ICE_TURARA, icicleSceneNum, actorParams);

    if (location->GetRandomizerCheck() == RC_UNKNOWN_CHECK) {
        LUSLOG_WARN("IdentifyIcicle did not receive a valid RC value (%d).", location->GetRandomizerCheck());
        assert(false);
    } else {
        icicleIdentity.randomizerInf = rcToRandomizerInf[location->GetRandomizerCheck()];
        icicleIdentity.randomizerCheck = location->GetRandomizerCheck();
    }

    return icicleIdentity;
}

CheckIdentity Randomizer::IdentifyRedIce(s32 sceneNum, s32 posX, s32 posZ) {
    struct CheckIdentity redIceIdentity;
    uint32_t redIceSceneNum = sceneNum;

    // Handle KZ moving
    if (sceneNum == SCENE_ZORAS_DOMAIN && LINK_IS_ADULT && posX == 531 && posZ == -1818) {
        posX = 628;
    }

    redIceIdentity.randomizerInf = RAND_INF_MAX;
    redIceIdentity.randomizerCheck = RC_UNKNOWN_CHECK;

    s32 actorParams = TWO_ACTOR_PARAMS(posX, posZ);

    Rando::Location* location = GetCheckObjectFromActor(ACTOR_BG_ICE_SHELTER, redIceSceneNum, actorParams);

    if (location->GetRandomizerCheck() == RC_UNKNOWN_CHECK) {
        LUSLOG_WARN("IdentifyRedIce did not receive a valid RC value (%d).", location->GetRandomizerCheck());
        assert(false);
    } else {
        redIceIdentity.randomizerInf = rcToRandomizerInf[location->GetRandomizerCheck()];
        redIceIdentity.randomizerCheck = location->GetRandomizerCheck();
    }

    return redIceIdentity;
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
    Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
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
    Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();

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

    // Use ITEM_KEY_BOSS to timestamp Ganon's boss key
    if (item == RG_GANONS_CASTLE_BOSS_KEY) {
        gSaveContext.ship.stats.itemTimestamp[ITEM_KEY_BOSS] = time;
        return;
    }

    if (randomizerGetToStatsTimeStamp.contains((RandomizerGet)item)) {
        gSaveContext.ship.stats.itemTimestamp[randomizerGetToStatsTimeStamp[(RandomizerGet)item]] = time;
        return;
    }

    // Count any bottled item as a bottle
    if (item >= RG_EMPTY_BOTTLE && item <= RG_BOTTLE_WITH_BIG_POE) {
        if (gSaveContext.ship.stats.itemTimestamp[ITEM_BOTTLE] == 0) {
            gSaveContext.ship.stats.itemTimestamp[ITEM_BOTTLE] = time;
        }
        return;
    }

    // Count any bombchu pack as bombchus
    if ((item >= RG_BOMBCHU_5 && item <= RG_BOMBCHU_20) || item == RG_PROGRESSIVE_BOMBCHU_BAG) {
        if (gSaveContext.ship.stats.itemTimestamp[ITEM_BOMBCHU] = 0) {
            gSaveContext.ship.stats.itemTimestamp[ITEM_BOMBCHU] = time;
        }
        return;
    }

    if (item == RG_MAGIC_SINGLE) {
        gSaveContext.ship.stats.itemTimestamp[ITEM_SINGLE_MAGIC] = time;
        return;
    }

    if (item == RG_DOUBLE_DEFENSE) {
        gSaveContext.ship.stats.itemTimestamp[ITEM_DOUBLE_DEFENSE] = time;
        return;
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
                    gSaveContext.ship.stats.itemTimestamp[TIMESTAMP_TRIFORCE_COMPLETED] =
                        static_cast<u32>(GAMEPLAYSTAT_TOTAL_TIME);
                    gSaveContext.ship.stats.gameComplete = 1;
                    Play_PerformSave(play);
                    Notification::Emit({
                        .message = "Game autosaved",
                    });
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
            ExtInv_SetItemById(ITEM_ROCS_FEATHER_SKIJER);
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
