/**
 * @file
 * @author Charles Averill
 * @date   12-May-2022
 * @brief Logic for the game manager, primarily the game loop and some graph functions
*/

#include "manager.h"

/**
 * Creates new game manager
 * @param  args                   Command-line arguments read with argp
 * @param  game_map               The processed game map
 * @return          Pointer to the new game manager
 */
game_manager *new_game(const arguments args, map *game_map)
{
    game_manager *manager = (game_manager *)malloc(sizeof(game_manager));

    // Team morale
    manager->morale = args.n_players > 3 ? 20 : 15;

    // Map setup
    manager->game_map = game_map;

    // Initialize Xenomorph and Ash locations
    manager->xenomorph_location = manager->game_map->xenomorph_start_room;
    if (args.use_ash) {
        manager->ash_location = manager->game_map->ash_start_room;
    } else {
        manager->ash_location = NULL;
    }

    // Place initial scrap
    for (int i = 0; i < manager->game_map->scrap_room_count; i++) {
        if (manager->game_map->scrap_rooms[i] == manager->ash_location) {
            continue;
        }

        manager->game_map->scrap_rooms[i]->num_scrap = 2;
    }

    // Place initial events
    for (int i = 0; i < manager->game_map->event_room_count; i++) {
        manager->game_map->event_rooms[i]->has_event = true;
    }

    // Place initial coolant
    for (int i = 0; i < manager->game_map->coolant_room_count; i++) {
        manager->game_map->coolant_rooms[i]->room_items[manager->game_map->coolant_rooms[i]->num_items++] =
            new_item(COOLANT_CANISTER);
    }

    // Game setup
    manager->round_index = 1;
    manager->turn_index = 0;

    // Character selection
    if (args.n_characters == 5) {
        for (int i = 0; i < 5; i++) {
            manager->characters[i] = &characters[i];
            manager->characters[i]->current_room = manager->game_map->player_start_room;
        }
    } else {
        int picked[5] = {-1, -1, -1, -1, -1};
        for (int i = 0; i < 5; i++) {
            manager->characters[i] = NULL;
        }
        for (int i = 0; i < args.n_characters; i++) {
            printf("Pick character %d:\n", i + 1);

            for (int j = 0; j < 5; j++) {
                int already_picked = 0;
                for (int k = 0; k < 5; k++) {
                    if (picked[k] == j) {
                        already_picked = 1;
                    }
                }
                if (already_picked) {
                    continue;
                }

                printf("%d) %s, %s - %d Actions - Special Ability: %s\n",
                       j + 1,
                       characters[j].last_name,
                       characters[j].first_name,
                       characters[j].max_actions,
                       characters[j].ability_description);
            }
            printf("e) Exit\n");

            char ch = '\0';
            while (ch < '1' || ch > '5') {
                ch = get_character();
                if (ch == 'e') {
                    exit(0);
                }
            }

            int selection = ch - '0' - 1;
            picked[i] = selection;
            manager->characters[i] = &characters[selection];

            manager->characters[i]->current_room = manager->game_map->player_start_room;
        }
    }
    manager->character_count = args.n_characters;

    // Set random seed
    srand(time(0));

    // Get objectives
    manager->num_objectives = manager->character_count + 1;
    manager->num_objectives = 1;
    manager->game_objectives = get_objectives(manager->num_objectives);
    for (int i = 0; i < manager->num_objectives; i++) {
        room *tmp = get_room(manager->game_map, manager->game_objectives[i].location_name);
        if (tmp != NULL) {
            manager->game_objectives[i].location = tmp;
        } else {
            printf("[WARNING] - Objective room names are hardcoded, should have a room of name %s.\nSetting location "
                   "to %s.\n",
                   manager->game_objectives[i].location_name,
                   manager->game_map->player_start_room->name);
            manager->game_objectives[i].location = manager->game_map->player_start_room;
        }
    }
    manager->is_final_mission = false;
    manager->final_mission_type = -1;

    // Jonesy setup
    manager->jonesy_caught = false;

    // Shuffle encounter deck
    shuffle_encounters();

    return manager;
}

/**
 * Print the game objectives
 * @param manager  Game manager
 */
void print_game_objectives(game_manager *manager)
{
    printf("Objectives are:\n");
    for (int i = 0; i < manager->num_objectives; i++) {
        printf("\t");
        print_objective_description(manager->game_objectives[i]);
    }
}

/**
 * Check uncleared objectives and clear them if conditions are met
 * @param manager  Game manager
 */
void update_objectives(game_manager *manager)
{
    for (int i = 0; i < manager->num_objectives; i++) {
        if (!manager->game_objectives[i].completed) {
            switch (manager->game_objectives[i].type) {
            case BRING_ITEM_TO_LOCATION:
                for (int j = 0; j < manager->character_count; j++) {
                    if (manager->characters[j]->current_room == manager->game_objectives[i].location &&
                        character_has_item(manager->characters[j], manager->game_objectives[i].target_item_type)) {
                        complete_objective(&(manager->game_objectives[i]));
                        break;
                    }
                }
                break;
            case CREW_AT_LOCATION_WITH_MINIMUM_SCRAP:;
                bool all_at_location_with_scrap = true;
                for (int j = 0; j < manager->character_count; j++) {
                    if (manager->characters[j]->current_room != manager->game_objectives[i].location ||
                        manager->characters[j]->num_scrap < manager->game_objectives[i].minimum_scrap) {
                        all_at_location_with_scrap = false;
                    }
                }

                if (all_at_location_with_scrap) {
                    complete_objective(&(manager->game_objectives[i]));
                }
                break;
            case DROP_COOLANT:;
                int coolant_count = 0;
                for (int j = 0; j < NUM_ROOM_ITEMS; j++) {
                    if (manager->game_objectives[i].location->room_items[j] != NULL &&
                        manager->game_objectives[i].location->room_items[j]->type == COOLANT_CANISTER) {
                        coolant_count++;
                    }
                }

                if (coolant_count >= 2) {
                    complete_objective(&(manager->game_objectives[i]));
                }
                break;
            }
        }
    }

    // Check for final mission
    if (!manager->is_final_mission) {
        bool all_complete = true;
        for (int i = 0; i < manager->num_objectives; i++) {
            all_complete &= manager->game_objectives[i].completed;
        }

        if (all_complete) {
            printf("[OBJECTIVE] - Completed all objectives\n");
            manager->is_final_mission = true;
            do {
                manager->final_mission_type = randint(0, NUM_FINAL_MISSIONS - 1);
            } while (manager->character_count == 1 && (manager->final_mission_type == CUT_OFF_EVERY_BULKHEAD_AND_VENT ||
                                                       manager->final_mission_type == BLOW_IT_OUT_INTO_SPACE));
            manager->final_mission_type = WERE_GOING_TO_BLOW_UP_THE_SHIP;
            setup_final_mission(manager);
        }
    } else {
        update_final_mission(manager);
    }
}

/**
 * Set up the final mission
 * @param manager  Game manager
 */
void setup_final_mission(game_manager *manager)
{
    printf("[FINAL OBJECTIVE] - You have a new mission!\n");
    printf("-----%s------\n", final_mission_names[manager->final_mission_type]);
    printf("%s\n", final_mission_desc[manager->final_mission_type]);

    switch (manager->final_mission_type) {
    case YOU_HAVE_MY_SYMPATHIES:;
        // Fill equipment storage or galley with coolant
        room *yequipment_storage = get_room(manager->game_map, "EQUIPMENT STORAGE");
        if (yequipment_storage == NULL) {
            yequipment_storage = manager->game_map->player_start_room;
        }
        for (int i = 0; i < manager->character_count + 2; i++) {
            yequipment_storage->room_items[i] = new_item(COOLANT_CANISTER);
        }
        yequipment_storage->num_items = max(yequipment_storage->num_items, manager->character_count + 2);
        // Put Ash at MU-TH-UR
        manager->ash_location = get_room(manager->game_map, "MU-TH-UR");
        if (manager->ash_location == NULL) {
            manager->ash_location = manager->game_map->ash_start_room;
        }
        manager->ash_health = 3;
        manager->ash_killed = false;
        break;
    case ESCAPE_ON_THE_NARCISSUS:;
        // Fill equipment storage or galley with coolant
        room *eequipment_storage = get_room(manager->game_map, "EQUIPMENT STORAGE");
        if (eequipment_storage == NULL) {
            eequipment_storage = manager->game_map->player_start_room;
        }
        for (int i = 0; i < manager->character_count + 2; i++) {
            eequipment_storage->room_items[i] = new_item(COOLANT_CANISTER);
        }
        eequipment_storage->num_items = max(eequipment_storage->num_items, manager->character_count + 2);
        break;
    case BLOW_IT_OUT_INTO_SPACE:
        // Replace and shuffle encounters
        replace_all_encounters();
        shuffle_encounters();
        break;
    case WERE_GOING_TO_BLOW_UP_THE_SHIP:;
        // Fill equipment storage or galley with coolant
        room *wequipment_storage = get_room(manager->game_map, "EQUIPMENT STORAGE");
        if (wequipment_storage == NULL) {
            wequipment_storage = manager->game_map->player_start_room;
        }
        for (int i = 0; i < manager->character_count + 2; i++) {
            wequipment_storage->room_items[i] = new_item(COOLANT_CANISTER);
        }
        wequipment_storage->num_items = max(wequipment_storage->num_items, manager->character_count + 2);
        // Give self destruct tracker to active character
        manager->active_character->self_destruct_tracker = 4;
        break;
    case CUT_OFF_EVERY_BULKHEAD_AND_VENT:
        // Add events to every named room
        for (int i = 0; i < manager->game_map->named_room_count; i++) {
            manager->game_map->rooms[manager->game_map->named_room_indices[i]]->has_event = true;
        }
        // Give self destruct tracker to active character
        manager->active_character->self_destruct_tracker = 4;
        break;
    }
}

/**
 * Check if final mission criteria have been met, win game if so
 * @param manager  Game manager
 */
void update_final_mission(game_manager *manager)
{
    if (!manager->is_final_mission) {
        return;
    }

    bool game_won = false;

    switch (manager->final_mission_type) {
    case YOU_HAVE_MY_SYMPATHIES:
        break;
    case ESCAPE_ON_THE_NARCISSUS:;
        // Win when all members in docking bay with 1 coolant dropped in docking bay each, with cat carrier and incinerator in inventory
        room *docking_bay = get_room(manager->game_map, "DOCKING BAY");
        if (docking_bay == NULL) {
            docking_bay = manager->game_map->player_start_room;
        }

        // Check coolant
        int num_canisters = 0;
        for (int i = 0; i < NUM_ROOM_ITEMS; i++) {
            if (docking_bay->room_items[i] != NULL && docking_bay->room_items[i]->type == COOLANT_CANISTER) {
                num_canisters++;
            }
        }
        bool enough_dropped_canisters = num_canisters >= manager->character_count;

        // Check inventory and location
        bool all_in_docking_bay = true;
        bool has_carrier = false;
        bool has_incinerator = false;
        for (int i = 0; i < manager->character_count; i++) {
            if (manager->characters[i]->current_room != docking_bay) {
                all_in_docking_bay = false;
            }
            if (character_has_item(manager->characters[i], CAT_CARRIER)) {
                has_carrier = true;
            }
            if (character_has_item(manager->characters[i], INCINERATOR)) {
                has_incinerator = true;
            }
        }

        if (enough_dropped_canisters && has_carrier && has_incinerator && all_in_docking_bay) {
            win_game(manager);
        }
        break;
    case BLOW_IT_OUT_INTO_SPACE:
        // Win if alien is in or adjacent to DOCKING BAY, a crew member is at AIRLOCK and another at BRIDGE,
        // and an alien is encountered (see trigger_encounter)
        break;
    case WERE_GOING_TO_BLOW_UP_THE_SHIP:
        // Win if all members are in airlock with 1 coolant canister and 1 scrap each
        game_won = true;

        // Get airlock or galley
        room *airlock = get_room(manager->game_map, "AIRLOCK");
        if (airlock == NULL) {
            airlock = manager->game_map->player_start_room;
        }

        for (int i = 0; i < manager->character_count; i++) {
            if (manager->characters[i]->current_room != airlock || manager->characters[i]->num_scrap == 0 || manager->characters[i]->coolant == NULL) {
                game_won = false;
            }
        }
        break;
    case CUT_OFF_EVERY_BULKHEAD_AND_VENT:
        // Win if all events are gone
        game_won = true;
        for (int i = 0; i < manager->game_map->named_room_count; i++) {
            if (manager->game_map->rooms[manager->game_map->named_room_indices[i]]->has_event) {
                game_won = false;
            }
        }
        break;
    }

    if (game_won) {
        win_game(manager);
    }
}

/**
 * Win the game
 */
void win_game(game_manager *manager)
{
    printf("[FINAL OBJECTIVE] - Complete! You Win!\n");
    exit(0);
}

/**
 * Move a character
 * @param  manager                     Game manager
 * @param  to_move                     Character to move
 * @param  allowed_moves               room_queue containing allowed destinations
 * @param  allow_back                  Whether or not to allow the user to exit the movement menu
 * @return               Pointer to the room the player moved the character to
 */
room *character_move(game_manager *manager, struct character *to_move, room_queue *allowed_moves, bool allow_back)
{
    // Get selection
    char ch = '\0';
    while (1) {
        // Print choices
        printf("Destinations:\n");
        if (allowed_moves == NULL) {
            // Move to adjacent rooms
            for (int i = 0; i < to_move->current_room->connection_count; i++) {
                printf("\t%d) %s\n", i + 1, to_move->current_room->connections[i]->name);
            }
            if (to_move->current_room->ladder_connection != NULL) {
                printf("\tl) Ladder to %s\n", to_move->current_room->ladder_connection->name);
            }
        } else {
            // Move to rooms defined in allowed_moves
            for (int i = 0; i < allowed_moves->size; i++) {
                printf("\t%d) %s\n", i + 1, poll_position(allowed_moves, i)->name);
            }
        }
        if (allow_back) {
            printf("\tb) Back\n");
        }

        // Get input
        ch = get_character();

        int max_destination_index =
            allowed_moves == NULL ? to_move->current_room->connection_count : allowed_moves->size;

        update_objectives(manager);

        if (allow_back && ch == 'b') {
            return to_move->current_room;
        } else if (allowed_moves == NULL && ch == 'l') {
            return to_move->current_room->ladder_connection;
        } else if (ch >= '0' && ch <= max_destination_index + '0') {
            if (allowed_moves == NULL) {
                return to_move->current_room->connections[ch - '0' - 1];
            } else {
                return poll_position(allowed_moves, ch - '0' - 1);
            }
        }
    }

    // Check Escape on the Narcissus final mission
    if (manager->is_final_mission && manager->final_mission_type == ESCAPE_ON_THE_NARCISSUS) {
    }

    return NULL;
}

/**
 * Find the shortest path between two rooms using a modified Djikstra's algorithm
 * @param  game_map               Game map
 * @param  source                 Source room
 * @param  target                 Target room
 * @return          A room queue containing the path [target ... source]
 */
room_queue *shortest_path(map *game_map, room *source, room *target)
{
    // Set all to unvisited and distance to inf
    reset_search(game_map, INT_MAX);
    // Set start vertex (xeno room) distance to 0
    source->search_distance = 0;

    // Create queue of rooms
    room_queue *rq = new_room_queue(64);
    for (int i = 0; i < game_map->room_count; i++) {
        push(rq, game_map->rooms[i]);
    }

    while (rq->head != NULL) {
        // Get closest node
        int min_node_distance = INT_MAX;
        room *min_node = rq->head;
        room *tmp = rq->head;
        while (tmp != NULL) {
            if (tmp->search_distance < min_node_distance) {
                min_node_distance = tmp->search_distance;
                min_node = tmp;
            }
            tmp = tmp->room_queue_next;
        }

        if (min_node == target) {
            room_queue *shortest_path = new_room_queue(64);
            if (min_node->search_previous_room != NULL || min_node == source) {
                while (min_node != NULL) {
                    push(shortest_path, min_node);
                    min_node = min_node->search_previous_room;
                }

                free(rq);
                return shortest_path;
            }
        }

        // Remove closest node from rq
        if (min_node == rq->head) {
            rq->head = rq->head->room_queue_next;
        } else {
            room *traverse = rq->head;
            while (traverse->room_queue_next != min_node) {
                traverse = traverse->room_queue_next;
            }
            traverse->room_queue_next = min_node->room_queue_next;
        }

        // For each connection of min_node in rq
        if (min_node != NULL) {
            for (int i = 0; i < min_node->connection_count; i++) {
                room *neighbor = min_node->connections[i];
                if (queue_contains(rq, neighbor)) {
                    int alt = min_node->search_distance + 1;
                    if (alt < neighbor->search_distance) {
                        neighbor->search_distance = alt;
                        neighbor->search_previous_room = min_node;
                    }
                }
            }

            if (queue_contains(rq, min_node->ladder_connection)) {
                int alt = min_node->search_distance + 1;
                if (alt < min_node->ladder_connection->search_distance) {
                    min_node->ladder_connection->search_distance = alt;
                    min_node->ladder_connection->search_previous_room = min_node;
                }
            }
        }
    }

    free(rq);
    return NULL;
}

/**
 * Move the xenomorph towards the nearest player and check for interceptions
 * @param  manager                   Game manager
 * @param  num_spaces                Maximum number of spaces to move the xenomorph
 * @param  morale_drop               Number of morale points to lose if an interception occurs
 * @return          True if the xenomorph intercepted a player, false otherwise
 */
bool xeno_move(game_manager *manager, int num_spaces, int morale_drop)
{
    // Get shortest path to a character
    room_queue *shortest = NULL;
    int s = INT_MAX;
    for (int i = 0; i < manager->character_count; i++) {
        room_queue *rq =
            shortest_path(manager->game_map, manager->xenomorph_location, manager->characters[i]->current_room);

        if (rq != NULL && rq->size < s) {
            if (shortest != NULL) {
                free(shortest);
            }

            shortest = rq;
            s = rq->size;
        }
    }

    // Move xeno along path
    if (s < num_spaces) {
        manager->xenomorph_location = shortest->head;
    } else if (num_spaces > 0) {
        for (int i = 0; i < num_spaces && shortest->head->room_queue_next != NULL; i++) {
            pop_tail(shortest);
        }
        manager->xenomorph_location = shortest->tail;
    }

    // Check if xeno intercepts characters
    bool printed_message = false;
    for (int i = 0; i < manager->character_count; i++) {
        if (manager->characters[i]->current_room == manager->xenomorph_location) {
            if (!printed_message) {
                printed_message = true;
                printf("The Xenomorph meets you in %s!\n", manager->xenomorph_location->name);
            }

            reduce_morale(manager, morale_drop, true);
            flee(manager, manager->characters[i]);
        }
    }

    free(shortest);

    return printed_message;
}

/**
 * Move Ash towards the nearest player, stop if he reaches Scrap
 * @param  manager                  Game manager
 * @param  num_spaces               Maximum number of spaces to move Ash
 * @return          True if Ash intercepted a player, false otherwise
 */
bool ash_move(game_manager *manager, int num_spaces)
{
    if (manager->ash_location == NULL || manager->ash_killed) {
        return false;
    }

    if (manager->is_final_mission && manager->final_mission_type != YOU_HAVE_MY_SYMPATHIES) {
        manager->ash_location->num_scrap = 0;
    }

    // Get shortest path to a character or a room with scrap
    room_queue *shortest = NULL;
    int s = INT_MAX;
    // Scrap check
    for (int i = 0; manager->final_mission_type != YOU_HAVE_MY_SYMPATHIES && i < manager->game_map->room_count; i++) {
        if (manager->game_map->rooms[i]->num_scrap == 0) {
            continue;
        }

        room_queue *rq = shortest_path(manager->game_map, manager->ash_location, manager->game_map->rooms[i]);

        if (rq != NULL && rq->size < s) {
            if (shortest != NULL) {
                free(shortest);
            }

            shortest = rq;
            s = rq->size;
        }
    }
    // Character check
    for (int i = 0; i < manager->character_count; i++) {
        // Ash only moves if nobody is with him
        if (manager->is_final_mission && manager->final_mission_type != YOU_HAVE_MY_SYMPATHIES &&
            manager->characters[i]->current_room == manager->ash_location) {
            if (shortest != NULL) {
                free(shortest);
            }

            return false;
        }

        room_queue *rq = shortest_path(manager->game_map, manager->ash_location, manager->characters[i]->current_room);

        if (rq != NULL && rq->size < s) {
            if (shortest != NULL) {
                free(shortest);
            }

            shortest = rq;
            s = rq->size;
        }
    }

    // Move Ash along path
    if (s < num_spaces) {
        manager->ash_location = shortest->head;
        if (manager->final_mission_type != YOU_HAVE_MY_SYMPATHIES) {
            manager->ash_location->num_scrap = 0;
        }

        ash_move(manager, num_spaces - s);
    } else if (num_spaces > 0) {
        for (int i = 0; i < num_spaces && shortest->head->room_queue_next != NULL; i++) {
            pop_tail(shortest);
        }
        manager->ash_location = shortest->tail;
    }

    if (manager->final_mission_type != YOU_HAVE_MY_SYMPATHIES) {
        manager->ash_location->num_scrap = 0;
    }

    // Check if Ash intercepts characters
    bool printed_message = false;
    for (int i = 0; i < manager->character_count; i++) {
        if (manager->characters[i]->current_room == manager->ash_location) {
            if (!printed_message) {
                printed_message = true;
                printf("Ash meets you in %s!\n", manager->ash_location->name);
            }

            if (manager->final_mission_type != YOU_HAVE_MY_SYMPATHIES) {
                if (manager->characters[i]->num_scrap > 0) {
                    printf("%s loses 1 Scrap!\n", manager->characters[i]->last_name);
                    manager->characters[i]->num_scrap--;
                } else {
                    printf("%s has no Scrap!\n", manager->characters[i]->last_name);
                    reduce_morale(manager, 1, false);
                }
            } else {
                if (character_has_item(manager->characters[i], COOLANT_CANISTER)) {
                    printf("%s uses COOLANT CANISTER to hurt Ash!\n", manager->characters[i]->last_name);

                    free(manager->characters[i]->coolant);
                    manager->characters[i]->coolant = NULL;

                    manager->ash_health -= 1;

                    // Move Ash
                    room_queue *ash_locations =
                        find_rooms_by_distance(manager->game_map, manager->ash_location, 3, false);

                    printf("Where to send Ash to?\n");
                    for (int i = 0; i < ash_locations->size; i++) {
                        printf("\t%d) %s\n", i + 1, poll_position(ash_locations, i)->name);
                    }

                    char ch = '\0';
                    while (ch < '1' || ch > '0' + ash_locations->size) {
                        ch = get_character();
                    }

                    ch = ch - '0' - 1;
                    printf("Ash retreats to %s!\n", poll_position(ash_locations, ch)->name);
                    manager->ash_location = poll_position(ash_locations, ch);
                    i = 0;

                    free(ash_locations);
                } else {
                    reduce_morale(manager, 3, false);
                    flee(manager, manager->characters[i]);
                }

                check_ash_health(manager);
            }
        }
    }

    free(shortest);

    return printed_message;
}

void check_ash_health(game_manager *manager)
{
    if (manager->is_final_mission && manager->final_mission_type == YOU_HAVE_MY_SYMPATHIES && !manager->ash_killed) {
        if (manager->ash_health == 0) {
            manager->ash_killed = true;
            printf("[FINAL OBJECTIVE] - You've killed Ash! Use an INCINERATOR on the Xenomorph to escape!\n");
        } else {
            printf("[FINAL OBJECTIVE] - Ash health = %d\n", manager->ash_health);
        }
    }
}

/**
 * Reduce the morale of the team and check for a game over
 * @param manager            Game manager
 * @param lost               Number of morale to lose
 * @param encountered_alien  True if alien reduced morale else false
 * @return          Number of morale lost
 */
int reduce_morale(game_manager *manager, int lost, bool encountered_alien)
{
    // Check for flashlights, electric prods, and cat carriers
    character *char_has_flashlight;
    bool has_flashlight = false;
    int flashlight_index;

    character *char_has_prod;
    bool has_prod = false;
    int prod_index;
    for (int i = 0; i < manager->character_count; i++) {
        for (int j = 0; j < 3; j++) {
            if (manager->characters[i]->held_items[j] != NULL &&
                manager->characters[i]->held_items[j]->type == FLASHLIGHT) {
                char_has_flashlight = manager->characters[i];
                flashlight_index = j;
                has_flashlight = true;
            } else if (encountered_alien && manager->characters[i]->held_items[j] != NULL &&
                       manager->characters[i]->held_items[j]->type == ELECTRIC_PROD) {
                char_has_prod = manager->characters[i];
                prod_index = j;
                has_prod = true;
            }
        }
    }

    char ch = '\0';
    if (has_flashlight ^ has_prod) {
        if (has_flashlight) {
            printf("%s has a FLASHLIGHT. Use it to reduce morale lost by 1? (y/n) ", char_has_flashlight->last_name);

            while (ch != 'y' && ch != 'n') {
                ch = get_character();
            }

            if (ch == 'y') {
                lost = min(0, lost - 1);
                use_item(char_has_flashlight, char_has_flashlight->held_items[flashlight_index]);
            }
        } else {
            printf("%s has an ELECTRIC PROD. Use it to reduce morale lost by 2? (y/n) ", char_has_prod->last_name);

            while (ch != 'y' && ch != 'n') {
                ch = get_character();
            }

            if (ch == 'y') {
                lost = min(0, lost - 2);
                use_item(char_has_prod, char_has_prod->held_items[prod_index]);
            }
        }
    } else if (has_flashlight && has_prod) {
        printf("An ELECTRIC PROD and FLASHLIGHT are held by ");

        if (char_has_flashlight == char_has_prod) {
            printf("%s.", char_has_flashlight->last_name);
        } else {
            printf("%s and %s.", char_has_flashlight->last_name, char_has_prod->last_name);
        }

        printf("\n\t1) Use ELECTRIC PROD\n\t2) Use FLASHLIGHT\n\tb) Do not use item\n");

        while (ch != '1' && ch != '2' && ch != 'b') {
            ch = get_character();
        }

        if (ch == '1') {
            lost = min(0, lost - 2);
            use_item(char_has_prod, char_has_prod->held_items[prod_index]);
        } else if (ch == '2') {
            lost = min(0, lost - 1);
            use_item(char_has_flashlight, char_has_flashlight->held_items[flashlight_index]);
        }
    }

    manager->morale -= lost;

    if (manager->morale <= 0) {
        printf("[GAME OVER] - Morale dropped to 0\n");
        exit(0);
    }

    return lost;
}

/**
 * Check for and trigger any events in the new location of the character `moved`
 * @param  manager               Game manager
 * @param  moved                 Character that was just moved
 * @param  using_motion_tracker  Whether or not the event is being triggered with a motion tracker
 * @return         0 if Safe event, 1 if Jonesy event, 2 if Xenomorph event, -1 if no event
 */
int trigger_event(game_manager *manager, struct character *moved, room *motion_tracker_room)
{
    bool is_motion_tracker = motion_tracker_room != NULL;
    room *target_room = is_motion_tracker ? motion_tracker_room : moved->current_room;
    if (target_room->has_event) {
        int event_type = randint(1, 12);
        target_room->has_event = false;

        if (event_type <= 8) {
            if (is_motion_tracker) {
                printf("All seems quiet...\n");
            } else {
                printf("[EVENT] - Safe\n");
            }

            return 0;
        } else if (event_type <= 10) {
            if (manager->jonesy_caught) {
                if (is_motion_tracker) {
                    printf("All seems quiet...\n");
                } else {
                    printf("[EVENT] - Safe\n");
                }

                return 0;
            } else {
                if (is_motion_tracker) {
                    printf("Something tiny makes a blip. Probably Jonesy.\n");
                } else {
                    printf("[EVENT] - Jonesy\n");
                    printf("Jonesy hisses at you!\n");
                }

                for (int i = 0; !is_motion_tracker && i < 3; i++) {
                    if (moved->held_items[i] != NULL && moved->held_items[i]->type == CAT_CARRIER) {
                        printf("%s has a CAT CARRIER - use it to catch Jonesy? (y/n) ", moved->last_name);

                        char ch = '\0';
                        while (ch != 'y' && ch != 'n') {
                            ch = get_character();
                        }

                        if (ch == 'y') {
                            printf("%s used the CAT CARRIER to catch Jonesy.\n", moved->last_name);
                            manager->jonesy_caught = true;
                            moved->held_items[i] = NULL;
                        }
                        break;
                    }
                }

                if (!is_motion_tracker && !manager->jonesy_caught) {
                    int dropped = reduce_morale(manager, 1, false);

                    if (dropped > 0) {
                        printf("Morale decreases by %d.\n", dropped);
                    }
                }
            }

            return 1;
        } else {
            if (is_motion_tracker) {
                printf("Something huge, and fast. Must be the Xenomorph.\n");

                manager->xenomorph_location = target_room;

                xeno_move(manager, 0, 2);
            } else {
                printf("[EVENT] - Surprise Attack\n");
                int lost_morale = randint(1, 2);
                printf("You encounter the Xenomorph!\n");

                manager->xenomorph_location = target_room;

                int dropped = reduce_morale(manager, lost_morale, true);

                if (dropped > 0) {
                    printf("Morale decreases by %d.\n", dropped);
                }

                flee(manager, moved);
            }

            return 2;
        }
    }

    return -1;
}

/**
 * Draw an encounter card and execute it
 * @param  manager               Game manager
 */
void trigger_encounter(game_manager *manager)
{
    int discard_index = draw_encounter();
    ENCOUNTER_TYPES encounter = discard_encounters[discard_index];

    // Check "Blow it out into space" final mission
    if (manager->final_mission_type == BLOW_IT_OUT_INTO_SPACE && encounter >= ALIEN_Lost_The_Signal &&
        encounter <= ALIEN_Hunt) {
        // Win if alien is in or adjacent to DOCKING BAY, a crew member is at AIRLOCK and another at BRIDGE,
        // and an alien is encountered
        room *docking_bay = get_room(manager->game_map, "DOCKING BAY");
        if (docking_bay == NULL) {
            docking_bay = manager->game_map->player_start_room;
        }

        room *airlock = get_room(manager->game_map, "AIRLOCK");
        if (airlock == NULL) {
            airlock = manager->game_map->ash_start_room;
        }

        room *bridge = get_room(manager->game_map, "BRIDGE");
        if (bridge == NULL) {
            bridge = manager->game_map->xenomorph_start_room;
        }

        bool xeno_in_right_place = manager->xenomorph_location == docking_bay;
        for (int i = 0; i < docking_bay->connection_count; i++) {
            xeno_in_right_place |= manager->xenomorph_location == docking_bay->connections[i];
        }

        bool airlock_right_place = false;
        bool bridge_right_place = false;
        for (int i = 0; i < manager->character_count; i++) {
            if (manager->characters[i]->current_room == airlock) {
                airlock_right_place = true;
            } else if (manager->characters[i]->current_room == bridge) {
                bridge_right_place = true;
            }
        }

        if (xeno_in_right_place && airlock_right_place && bridge_right_place) {
            win_game(manager);
        }
    }

    switch (encounter) {
    case -1:
    case QUIET:;
        room *target_room =
            manager->game_map
                ->rooms[manager->game_map->named_room_indices[randint(0, manager->game_map->named_room_count - 1)]];
        printf("[ENCOUNTER] - All is quiet in %s. Xenomorph moves 1 space.", target_room->name);
        if (manager->ash_location != NULL && !manager->ash_killed) {
            printf(" Ash moves 1 space.\n");
        } else {
            printf("\n");
        }

        int scrap_decider = randint(1, 11);
        if (scrap_decider <= 8) {
            target_room->num_scrap += 2;
        } else if (scrap_decider <= 10) {
            target_room->num_scrap += 3;
        } else {
            target_room->num_scrap += 1;
        }

        if (manager->final_mission_type != CUT_OFF_EVERY_BULKHEAD_AND_VENT) {
            target_room->has_event = true;
        }

        xeno_move(manager, 1, 2);
        ash_move(manager, 1);

        break;
    case ALIEN_Lost_The_Signal:
        printf("[ENCOUNTER] - Lost the Signal - Xenomorph has returned to %s\n",
               manager->game_map->xenomorph_start_room->name);

        manager->xenomorph_location = manager->game_map->xenomorph_start_room;
        xeno_move(manager, 0, 2);
        ash_move(manager, 1);
        replace_alien_cards();

        break;
    case ALIEN_Stalk:
        printf("[ENCOUNTER] - The Xenomorph is stalking...\n");
        xeno_move(manager, 3, 3);
        ash_move(manager, 1);

        break;
    case ALIEN_Hunt:
        printf("[ENCOUNTER] - The Xenomorph is hunting!\n");
        xeno_move(manager, 2, 4);
        ash_move(manager, 1);

        break;
    case ORDER937_Meet_Me_In_The_Infirmary:
        if (manager->ash_location != NULL && !manager->ash_killed) {
            printf("[ENCOUNTER] - Meet Me in the Infirmary - Ash moves twice, and %s moves to %s\n",
                   manager->active_character->last_name,
                   manager->game_map->ash_start_room->name);
        } else {
            printf("[ENCOUNTER] - Meet Me in the Infirmary - %s moves to %s\n",
                   manager->active_character->last_name,
                   manager->game_map->ash_start_room->name);
        }

        manager->active_character->current_room = manager->game_map->ash_start_room;
        update_objectives(manager);
        ash_move(manager, 2);

        break;
    case ORDER937_Crew_Expendable:
        if (manager->ash_location != NULL && !manager->ash_killed) {
            printf("[ENCOUNTER] - Crew Expendable - Ash moves twice, and %s loses all Scrap\n",
                   manager->active_character->last_name);
        } else {
            printf("[ENCOUNTER] - Crew Expendable - %s loses all Scrap\n", manager->active_character->last_name);
        }

        replace_order937_cards();
        ash_move(manager, 2);
        manager->active_character->num_scrap = 0;

        break;
    case ORDER937_Collating_Data:
        if (manager->ash_location != NULL && !manager->ash_killed) {
            printf("[ENCOUNTER] - Collating Data - Ash moves twice, and each character loses 1 "
                   "Scrap\n");
        } else {
            printf("[ENCOUNTER] - Collating Data - Each character loses 1 Scrap\n");
        }

        for (int i = 0; i < manager->character_count; i++) {
            manager->characters[i]->num_scrap = max(0, manager->characters[i]->num_scrap - 1);
        }
        ash_move(manager, 2);

        break;
    default:
        printf("[ERROR] - Unknown encounter type %d\n", encounter);
        break;
    }
}

/**
 * Force a character to move 3 spaces away
 * @param manager  Game manager
 * @param moved    Character to flee
 */
void flee(game_manager *manager, struct character *moved)
{
    printf("%s must flee 3 spaces:\n", moved->last_name);

    room_queue *allowed_moves = find_rooms_by_distance(manager->game_map, moved->current_room, 3, false);
    moved->current_room = character_move(manager, moved, allowed_moves, false);
    update_objectives(manager);
    free(allowed_moves);
}

/**
 * Pickup handler
 * @param manager  Game manager
 * @return         False if break_loop is false, else true
 */
bool pickup(game_manager *manager)
{
    bool break_loop = false;

    if (manager->active_character->current_room->num_scrap == 0 &&
        manager->active_character->current_room->num_items == 0) {
        printf("There are no items or Scrap to pick up.\n");
    } else {
        // Print out options
        printf("Pick up options:\n");
        int option_index = 0;

        // Print scrap
        int scrap_index = -1;
        if (manager->active_character->current_room->num_scrap != 0) {
            scrap_index = option_index;
            printf("\t%d) Scrap (%d)\n", ++option_index, manager->active_character->current_room->num_scrap);
        }

        // Print room items
        int item_indices[NUM_ROOM_ITEMS] = {-1, -1, -1, -1, -1, -1};
        for (int k = 0; k < manager->active_character->current_room->num_items; k++) {
            if (manager->active_character->current_room->room_items[k] != NULL) {
                item_indices[k] = option_index;
                printf("\t%d) ", ++option_index);
                print_item(manager->active_character->current_room->room_items[k]);
            }
        }

        // Back
        printf("\tb) Back\n");

        // Read input
        char ch = '\0';
        while (ch < '1' || ch > '0' + option_index) {
            ch = get_character();

            if (ch == 'b') {
                break;
            }
        }

        // Process input
        if (ch == 'b') {
            return false;
        } else {
            int selection_index = ch - '0' - 1;

            if (scrap_index == selection_index) {
                printf("Pick up how much scrap? (Max %d): ", manager->active_character->current_room->num_scrap);

                ch = '\0';
                while (ch < '1' || ch > '0' + manager->active_character->current_room->num_scrap) {
                    ch = get_character();
                }

                printf("%s picked up %d Scrap\n", manager->active_character->last_name, ch - '0');
                manager->active_character->current_room->num_scrap -= ch - '0';
                manager->active_character->num_scrap += ch - '0';
                manager->active_character->num_scrap = min(9, manager->active_character->num_scrap);

                break_loop = true;
            } else {
                item *target_item = NULL;
                int m;
                for (m = 0; m < NUM_ROOM_ITEMS; m++) {
                    if (item_indices[m] == selection_index) {
                        target_item = manager->active_character->current_room->room_items[m];
                        break;
                    }
                }

                if (target_item->type == COOLANT_CANISTER) {
                    if (manager->active_character->coolant == NULL) {
                        printf("%s picked up the COOLANT CANISTER\n", manager->active_character->last_name);
                        manager->active_character->current_room->room_items[m] = NULL;
                        manager->active_character->coolant = target_item;
                        break_loop = true;
                    } else {
                        printf("%s is already holding a COOLANT CANISTER\n", manager->active_character->last_name);
                    }
                } else {
                    if (manager->active_character->num_items < 3) {
                        for (int l = 0; l < 3; l++) {
                            if (manager->active_character->held_items[l] == NULL) {
                                manager->active_character->held_items[l] = target_item;
                                break;
                            }
                        }
                        manager->active_character->num_items++;

                        printf("%s picked up the %s\n",
                               manager->active_character->last_name,
                               item_names[manager->active_character->current_room->room_items[m]->type]);

                        manager->active_character->current_room->num_items--;
                        manager->active_character->current_room->room_items[m] = NULL;

                        break_loop = true;
                    } else {
                        printf("%s is already holding 3 items\n", manager->active_character->last_name);
                    }
                }
            }
        }
    }

    return break_loop;
}

/**
 * Drop handler
 * @param manager  Game manager
 * @return         False if break_loop is false, else true
 */
bool drop(game_manager *manager)
{
    bool break_loop = false;

    if (manager->active_character->num_scrap == 0 && manager->active_character->num_items == 0 &&
        manager->active_character->coolant == NULL) {
        printf("%s has no items or Scrap to drop.\n", manager->active_character->last_name);
    } else {
        // Print out options
        printf("Drop options:\n");
        int option_index = 0;

        // Print scrap
        int scrap_index = -1;
        if (manager->active_character->num_scrap != 0) {
            scrap_index = option_index;
            printf("\t%d) Scrap (%d)\n", ++option_index, manager->active_character->num_scrap);
        }

        // Print character items
        int item_indices[3] = {-1, -1, -1};
        for (int k = 0; k < 3; k++) {
            if (manager->active_character->held_items[k] != NULL) {
                item_indices[k] = option_index;
                printf("\t%d) ", ++option_index);
                print_item(manager->active_character->held_items[k]);
            }
        }

        // Coolant
        int coolant_index = -1;
        if (manager->active_character->coolant != NULL) {
            coolant_index = option_index;
            printf("\t%d) ", ++option_index);
            print_item(manager->active_character->coolant);
        }

        // Back
        printf("\tb) Back\n");

        // Read input
        char ch = '\0';
        while (ch < '1' || ch > '0' + option_index) {
            ch = get_character();

            if (ch == 'b') {
                break;
            }
        }

        // Process input
        if (ch == 'b') {
            return false;
        } else {
            int selection_index = ch - '0' - 1;

            if (scrap_index == selection_index) {
                printf("Drop how much scrap? (Max %d): ", manager->active_character->num_scrap);

                ch = '\0';
                while (ch < '1' || ch > '0' + manager->active_character->num_scrap) {
                    ch = get_character();
                }

                printf("%s dropped %d Scrap\n", manager->active_character->last_name, ch - '0');
                manager->active_character->current_room->num_scrap += ch - '0';
                manager->active_character->num_scrap -= ch - '0';

                break_loop = true;
            } else if (manager->active_character->current_room->num_items < NUM_ROOM_ITEMS) {
                item *target_item = NULL;
                int k;
                for (k = 0; k < 3; k++) {
                    if (item_indices[k] == selection_index) {
                        target_item = manager->active_character->held_items[k];
                        break;
                    }
                }

                if (selection_index == coolant_index) {
                    printf("%s dropped a COOLANT CANISTER in %s\n",
                           manager->active_character->last_name,
                           manager->active_character->current_room->name);

                    for (int m = 0; m < NUM_ROOM_ITEMS; m++) {
                        if (manager->active_character->current_room->room_items[m] == NULL) {
                            manager->active_character->current_room->room_items[m] = manager->active_character->coolant;
                            break;
                        }
                    }

                    manager->active_character->coolant = NULL;
                    manager->active_character->current_room->num_items++;

                    break_loop = true;
                } else {
                    printf("%s dropped a %s in %s\n",
                           manager->active_character->last_name,
                           item_names[target_item->type],
                           manager->active_character->current_room->name);

                    for (int l = 0; l < NUM_ROOM_ITEMS; l++) {
                        if (manager->active_character->current_room->room_items[l] == NULL) {
                            manager->active_character->current_room->room_items[l] =
                                manager->active_character->held_items[k];
                            break;
                        }
                    }
                    manager->active_character->current_room->num_items++;
                    manager->active_character->num_items--;

                    manager->active_character->held_items[k] = NULL;

                    break_loop = true;
                }
            } else {
                printf("%s already has %d items\n", manager->active_character->current_room->name, NUM_ROOM_ITEMS);
            }
        }
    }

    return break_loop;
}

/**
 * Use item handler
 * @param  manager               Game manager
 * @return         0 if break_loop is false, 1 if it's true, 2 if it's true and don't do an encounter this turn
 */
int use(game_manager *manager)
{
    int break_loop = 0;

    int usable_indices[3];
    int num_usable = 0;
    for (int i = 0; i < 3; i++) {
        if (manager->active_character->held_items[i] != NULL && manager->active_character->held_items[i]->uses_action) {
            usable_indices[num_usable++] = i;
        }
    }

    if (num_usable == 0) {
        printf("%s has no items that can be used.\n", manager->active_character->last_name);
        return 0;
    } else {
        printf("Use options:\n");

        for (int i = 0; i < num_usable; i++) {
            printf("\t%d) ", i + 1);
            print_item(manager->active_character->held_items[usable_indices[i]]);
        }
        printf("\tb) Back\n");

        char ch = '\0';
        while (ch < '1' || ch > '0' + num_usable) {
            ch = get_character();

            if (ch == 'b') {
                break;
            }
        }

        if (ch == 'b') {
            return 0;
        } else {
            int item_selection = ch - '0' - 1;

            ITEM_TYPES item_type = manager->active_character->held_items[usable_indices[item_selection]]->type;

            switch (item_type) {
            case MOTION_TRACKER:;
                room_queue *within_2 =
                    find_rooms_by_distance(manager->game_map, manager->active_character->current_room, 2, true);

                int event_rooms[64];
                int num_event_rooms = 0;
                for (int i = 0; i < within_2->size; i++) {
                    if (poll_position(within_2, i) != manager->active_character->current_room &&
                        poll_position(within_2, i)->has_event) {
                        event_rooms[num_event_rooms++] = i;
                    }
                }

                if (num_event_rooms == 0) {
                    printf("There are no rooms with events nearby.\n");
                } else {
                    printf("Choose a room to check events:\n");
                    for (int i = 0; i < num_event_rooms; i++) {
                        printf("\t%d) %s\n", i + 1, poll_position(within_2, event_rooms[i])->name);
                    }
                    printf("\tb) Back\n");

                    ch = '\0';
                    while (ch < '1' || ch > '0' + num_event_rooms) {
                        ch = get_character();

                        if (ch == 'b') {
                            break;
                        }
                    }

                    if (ch == 'b') {
                        free(within_2);
                        return 0;
                    } else {
                        ch = ch - '0' - 1;
                        use_item(manager->active_character, manager->active_character->held_items[item_selection]);
                        trigger_event(manager, manager->active_character, poll_position(within_2, event_rooms[ch]));

                        break_loop = 1;
                    }
                }
                free(within_2);
                break;
            case GRAPPLE_GUN:;
                room_queue *grapple_q = shortest_path(
                    manager->game_map, manager->xenomorph_location, manager->active_character->current_room);
                if (grapple_q->size > 4) {
                    printf("The Xenomorph is not within 3 spaces.\n");
                    free(grapple_q);
                    return 0;
                } else {
                    free(grapple_q);
                    room_queue *alien_locations =
                        find_rooms_by_distance(manager->game_map, manager->xenomorph_location, 3, false);

                    printf("Where to send the Xenomorph to?\n");
                    for (int i = 0; i < alien_locations->size; i++) {
                        printf("\t%d) %s\n", i + 1, poll_position(alien_locations, i)->name);
                    }
                    printf("\tb) Back\n");

                    ch = '\0';
                    while (ch < '1' || ch > '0' + alien_locations->size) {
                        ch = get_character();

                        if (ch == 'b') {
                            break;
                        }
                    }

                    if (ch == 'b') {
                        free(alien_locations);
                        return 0;
                    } else {
                        ch = ch - '0' - 1;
                        use_item(manager->active_character, manager->active_character->held_items[item_selection]);
                        printf("The Xenomorph retreats to %s!\n", poll_position(alien_locations, ch)->name);
                        manager->xenomorph_location = poll_position(alien_locations, ch);
                    }
                    free(alien_locations);
                    break_loop = 1;
                }
                break;
            case INCINERATOR:;
                room_queue *incinerator_q = shortest_path(
                    manager->game_map, manager->xenomorph_location, manager->active_character->current_room);
                if (incinerator_q->size > 4) {
                    printf("The Xenomorph is not within 3 spaces.\n");
                    free(incinerator_q);
                    return 0;
                } else {
                    use_item(manager->active_character, manager->active_character->held_items[item_selection]);
                    printf("The Xenomorph retreats to %s!\n", manager->game_map->xenomorph_start_room->name);
                    manager->xenomorph_location = manager->game_map->xenomorph_start_room;

                    free(incinerator_q);

                    // Check "You Have My Sympathies" final mission
                    if (manager->is_final_mission && manager->final_mission_type == YOU_HAVE_MY_SYMPATHIES &&
                        manager->ash_killed) {
                        win_game(manager);
                    }

                    return 2;
                }

                break;
            }
        }
    }

    return break_loop;
}

/**
 * Main game loop, handles player input and game logic
 * @param manager  Game manager
 */
void game_loop(game_manager *manager)
{
    printf("--------------SITUATION CRITICAL---------------\n");
    printf("--------REPORT ISSUED BY DALLAS, ARTHUR--------\n");
    printf("An Alien is stalking us on board the           \n");
    printf("Nostromo, and Executive Officer Kane is        \n");
    printf("dead. The remaining crew and I are working     \n");
    printf("together to patch the ship and do what we      \n");
    printf("can to survive. I don't know if we'll make     \n");
    printf("it. The Alien is big, fast, and deadly, and    \n");
    printf("could be waiting just beyond the next hatch... \n");
    printf("-----------------------------------------------\n");
    print_game_objectives(manager);

    printf("Enter to start\n");
    get_character();

    while (1) {
        printf("-----Round %d-----\n", manager->round_index);

        for (int i = 0; i < 5; i++) {
            if (manager->characters[i] == NULL) {
                break;
            }

            manager->turn_index = i;

            manager->active_character = manager->characters[manager->turn_index];
            character *active = manager->active_character;
            printf("------Turn %d: %s------\n", manager->turn_index + 1, active->last_name);

            if (active->self_destruct_tracker > 0) {
                active->self_destruct_tracker--;

                if (active->self_destruct_tracker <= 0) {
                    printf("[SELF-DESTRUCT] The Self-Destruct timer drops to 0!\n");
                    printf("[GAME OVER] - The Nostromo self-destructed with the Crew still on it!\n");
                    exit(0);
                } else {
                    printf("[SELF-DESTRUCT] The Self-Destruct timer drops to %d!\n", active->self_destruct_tracker);
                }
            }

            // Some abilities can only be used once per turn
            bool used_ability = false;

            printf("h - view help menu\n");

            bool do_encounter = true;
            for (int j = active->max_actions; j > 0; j--) {
                active->current_actions = j;

                char choice = '\0';
                while (1) {

                    printf("Actions - %d/%d\n", active->current_actions, active->max_actions);

                    choice = get_character();

                    bool break_loop = false;
                    bool recognized = true;

                    switch (choice) {
                    case 'h':
                        printf("m - move\n"
                               "p - pick up\n"
                               "d - drop\n"
                               "a - ability\n"
                               "i - view inventory\n"
                               "k - view team info\n"
                               "c - craft\n"
                               "u - use item\n"
                               "g - give item\n"
                               "s - end turn early\n"
                               "v - view current room\n"
                               "l - character locations\n"
                               "%s"
                               "%s"
                               "q - draw map\n"
                               "r - print text map\n"
                               "e - exit\n",
                               manager->is_final_mission ? "o - print final objective\n"
                                                         : "o - print game objectives\n",
                               manager->final_mission_type == BLOW_IT_OUT_INTO_SPACE
                                   ? "n - discard scrap, view next encounter\n"
                                   : "");

                        break;
                    case 'm':; // Start case with assignment
                        room *last_room = active->current_room;
                        active->current_room = character_move(manager, active, NULL, true);
                        if (active->current_room == last_room) {
                            // Move canceled
                            printf("Canceled move\n");
                        } else {
                            // Move successful
                            printf("%s moved from %s to %s\n",
                                   active->last_name,
                                   last_room->name,
                                   active->current_room->name);

                            // Check for events in new location
                            if (trigger_event(manager, active, NULL) == 2) { // Alien encounter
                                // Immediately end turn and don't do an encounter
                                j = 0;
                                do_encounter = false;
                            }

                            // Check if player moved into xeno area
                            if (xeno_move(manager, 0, 2)) {
                                j = 0;
                                do_encounter = false;
                            }

                            // Check if player moved into ash area
                            ash_move(manager, 0);

                            update_objectives(manager);
                            update_final_mission(manager);

                            break_loop = true;
                        }

                        break;
                    case 'p':
                        break_loop = pickup(manager);
                        break;
                    case 'd':
                        break_loop = drop(manager);
                        update_objectives(manager);
                        update_final_mission(manager);
                        break;
                    case 'a':
                        if (!used_ability) {
                            printf("Using %s's ability: %s\n", active->last_name, active->ability_description);
                            ability_output *ao =
                                active->ability_function(manager->game_map, manager->characters, active);

                            break_loop = ao->use_action;
                            used_ability = !ao->can_use_ability_again;

                            if (ao->move_character_index >= 0) {
                                room *last_room = manager->characters[ao->move_character_index]->current_room;
                                manager->characters[ao->move_character_index]->current_room =
                                    character_move(manager, manager->characters[ao->move_character_index], NULL, false);
                                printf("%s moved %s from %s to %s\n",
                                       active->last_name,
                                       manager->characters[ao->move_character_index]->last_name,
                                       last_room->name,
                                       manager->characters[ao->move_character_index]->current_room->name);
                            }

                            free(ao);
                        } else {
                            printf("You may only use this ability once per turn.\n");
                        }

                        break;
                    case 'i':
                        print_inventory(active);

                        break;
                    case 'k':
                        printf("Team Morale: %d\n", manager->morale);
                        for (int m = 0; m < manager->character_count; m++) {
                            print_inventory(manager->characters[m]);
                        }
                        break;
                    case 'c':
                        if (active->num_scrap == 0) {
                            printf("%s has no Scrap\n", active->last_name);

                            break;
                        } else if (active->num_items == 3) {
                            printf("%s already has 3 items\n", active->last_name);

                            break;
                        }

                        printf("Craft Options:\n");

                        bool is_brett = active->ability_function == brett_ability;

                        int cost_reduction = is_brett ? 1 : 0;
                        int num_craftable = 0;
                        int craftable_indices[NUM_ITEM_TYPES];
                        int m;
                        for (m = 0; m < NUM_ITEM_TYPES; m++) {
                            int cost = item_costs[m];
                            if (cost >= 2) {
                                cost -= cost_reduction;
                            }

                            if (cost <= active->num_scrap && m != COOLANT_CANISTER) {
                                craftable_indices[num_craftable++] = m;
                                printf("\t%d) ", num_craftable);
                                print_item_type(m, cost >= 2 ? cost_reduction : 0);
                            }
                        }
                        printf("\tb) Back\n");

                        char ch = '\0';
                        while (ch < '1' || ch > '0' + num_craftable) {
                            ch = get_character();

                            if (ch == 'b') {
                                break;
                            }
                        }

                        if (ch == 'b') {
                            break;
                        } else {
                            ch = ch - '0' - 1;
                            ch = craftable_indices[ch];
                            for (int n = 0; n < 3; n++) {
                                if (active->held_items[n] == NULL) {
                                    active->held_items[n] = new_item(ch);
                                    break;
                                }
                            }
                            printf("%s crafted %s\n", active->last_name, item_names[ch]);
                            active->num_scrap -= item_costs[ch] - cost_reduction;
                            active->num_items++;
                        }

                        update_objectives(manager);
                        update_final_mission(manager);

                        break_loop = !is_brett;

                        break;
                    case 'u':;
                        int u = use(manager);
                        if (u != 0) {
                            break_loop = true;
                            do_encounter &= u != 2;
                        }

                        break;
                    case 'g':;
                        // Check if can give item
                        int tradeable_indices[5];
                        int num_tradeable = 0;
                        for (int m = 0; m < manager->character_count; m++) {
                            if (manager->characters[m] != active &&
                                manager->characters[m]->current_room == active->current_room &&
                                ((active->num_items > 0 && manager->characters[m]->num_items < 3) ||
                                 (active->coolant != NULL && active->coolant == NULL) || active->num_scrap > 0)) {
                                tradeable_indices[num_tradeable++] = m;
                            }
                        }

                        if (num_tradeable == 0) {
                            printf("Can't give anything right now.\n");
                        } else {
                            printf("Give options:\n");
                            printf("Characters:\n");

                            for (int m = 0; m < num_tradeable; m++) {
                                printf("\t%d) %s\n", m + 1, manager->characters[tradeable_indices[m]]->last_name);
                            }
                            printf("\tb) Back\n");

                            char ch = '\0';
                            while (ch < '1' || ch > '0' + num_tradeable) {
                                ch = get_character();

                                if (ch == 'b') {
                                    break;
                                }
                            }

                            character *give_target = manager->characters[tradeable_indices[ch - '0' - 1]];

                            if (ch != 'b') {
                                printf("Items:\n");

                                int item_indices[3];
                                int num_items = 0;
                                if (give_target->num_items < 3) {
                                    for (int m = 0; m < 3; m++) {
                                        if (active->held_items[m] != NULL) {
                                            printf("\t%d) ", m + 1);
                                            print_item(active->held_items[m]);

                                            item_indices[num_items++] = m;
                                        }
                                    }
                                }
                                if (active->coolant != NULL && give_target->coolant == NULL) {
                                    printf("\tc) ");
                                    print_item(active->coolant);
                                }
                                if (active->num_scrap > 0) {
                                    printf("\ts) Scrap (%d)\n", active->num_scrap);
                                }
                                printf("\tb) Back\n");

                                ch = '\0';
                                while (ch < '1' || ch > '0' + num_items || num_items == 0) {
                                    ch = get_character();

                                    if ((give_target->coolant == NULL && ch == 'c') ||
                                        (active->num_scrap > 0 && ch == 's') || ch == 'b') {
                                        break;
                                    }
                                }

                                if (ch == 'b') {
                                    break;
                                } else {
                                    if (give_target->coolant == NULL && ch == 'c') { // Give coolant
                                        printf("%s gave COOLANT CANISTER to %s\n",
                                               active->last_name,
                                               give_target->last_name);
                                        give_target->coolant = active->coolant;
                                        active->coolant = NULL;
                                        break_loop = true;
                                    } else if (active->num_scrap > 0 && ch == 's') { // Give scrap
                                        printf("How much? (Max %d) ", active->num_scrap);

                                        ch = '\0';
                                        while (ch < '1' || ch > '0' + active->num_scrap) {
                                            ch = get_character();
                                        }
                                        ch -= '0';

                                        active->num_scrap -= ch;
                                        give_target->num_scrap += ch;

                                        printf("%s gave %s %d Scrap.\n", active->last_name, give_target->last_name, ch);
                                        break_loop = true;
                                    } else { // Give item
                                        for (int m = 0; m < 3; m++) {
                                            if (give_target->held_items[m] == NULL) {
                                                give_target->held_items[m] = active->held_items[ch - '0' - 1];
                                                active->held_items[ch - '0' - 1] = NULL;
                                                break;
                                            }
                                        }

                                        printf("%s gave %s to %s\n",
                                               active->last_name,
                                               item_names[give_target->held_items[ch - '0' - 1]->type],
                                               give_target->last_name);

                                        active->num_items--;
                                        give_target->num_items++;

                                        break_loop = true;
                                    }
                                }
                            }
                        }

                        break;
                    case 's':
                        printf("%s's turn ends\n", active->last_name);
                        j = 0;
                        break_loop = true;

                        break;
                    case 'v':
                        print_room(active->current_room, 1);

                        break;
                    case 'l':
                        for (int i = 0; i < manager->character_count; i++) {
                            printf("%s at %s\n",
                                   manager->characters[i]->last_name,
                                   manager->characters[i]->current_room->name);
                        }
                        printf("Xenomorph at %s\n", manager->xenomorph_location->name);
                        if (manager->ash_location != NULL) {
                            printf("Ash at %s\n", manager->ash_location->name);
                        }

                        break;
                    case 'o':
                        if (manager->is_final_mission) {
                            printf("------%s------\n", final_mission_names[manager->final_mission_type]);
                            printf("%s\n", final_mission_desc[manager->final_mission_type]);
                            if (manager->final_mission_type == YOU_HAVE_MY_SYMPATHIES) {
                                printf("Ash health: %d\n", manager->ash_health);
                            }
                        } else {
                            print_game_objectives(manager);
                        }

                        break;
                    case 'n':
                        if (manager->final_mission_type == BLOW_IT_OUT_INTO_SPACE) {
                            if (manager->active_character->num_scrap == 0) {
                                printf("Must have at least 1 Scrap to use this ability.\n");
                                break_loop = false;
                            } else {
                                ability_output *ao = lambert_ability(manager->game_map, manager->characters, active);

                                break_loop = ao->use_action;
                                used_ability = false;

                                if (break_loop) {
                                    manager->active_character->num_scrap--;
                                }

                                free(ao);
                            }
                        } else {
                            recognized = false;
                        }
                        break;
                    case 'q':
                        printf("%s\n", manager->game_map->ascii_map);

                        break;
                    case 'r':
                        print_map(manager->game_map);

                        break;
                    case 'e':
                        printf("Are you sure you want to exit? Game progress will not be saved. "
                               "(y/n)\n");
                        if (get_character() == 'y') {
                            exit(0);
                        }

                        break;
                    default:
                        recognized = false;
                        break;
                    }

                    if (break_loop) {
                        break;
                    } else if (!recognized) {
                        printf("Unrecognized command\n");
                    }
                }
            }

            if (do_encounter) {
                trigger_encounter(manager);
            }
        }

        manager->round_index++;
    }
}
