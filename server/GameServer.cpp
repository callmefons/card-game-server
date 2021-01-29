#include "GameServer.h"
#include <stdlib.h>
#include <stdio.h>

static void InitGame(ServerRoomData *pSRoom);
static void ChangeRoomState(ServerRoomData *pSRoom, s8 state, u32 time);
static void PackGameRoomData(GameRoomData* pGameRoom, ServerUserData *pSUser, ServerRoomData *pSRoom, bool hideOther);
static ServerUserData *users;
static ServerRoomData *rooms;
static pthread_mutex_t register_mutex;

static void SetUserData(int dbIndex, UserData *pUser){
    pUser -> userId         = atoi(STSV_DBGetRowStringValue(dbIndex,0));
    strcpy(pUser -> uuid, STSV_DBGetRowStringValue(dbIndex,1));
    strcpy(pUser -> email, STSV_DBGetRowStringValue(dbIndex,2));
    strcpy(pUser -> name, STSV_DBGetRowStringValue(dbIndex,3));
    pUser->rankId           = atoi(STSV_DBGetRowStringValue(dbIndex, 4));
    pUser->characterId      = atoi(STSV_DBGetRowStringValue(dbIndex, 5)); // avatar
    pUser->gold             = atoi(STSV_DBGetRowStringValue(dbIndex, 6));
    pUser->exp              = atoi(STSV_DBGetRowStringValue(dbIndex, 7));
    pUser->level            = atoi(STSV_DBGetRowStringValue(dbIndex, 8));
    pUser->winCount         = atoi(STSV_DBGetRowStringValue(dbIndex, 9));
    pUser->loseCount        = atoi(STSV_DBGetRowStringValue(dbIndex, 10));
    pUser->bestScore        = atoi(STSV_DBGetRowStringValue(dbIndex, 11));
    pUser->timeRemainGold   = GetTimeMillisec() - atoi(STSV_DBGetRowStringValue(dbIndex, 12));
}
static void ResetUserData(UserData *pUser) {
    pUser->userId           = FREE_USER_SLOT;
    strcpy(pUser->name, "");
    strcpy(pUser->uuid, "");
    strcpy(pUser->email, "");
    pUser->rankId          =0;
    pUser->characterId      = 1;
    pUser->gold             = 0;
    pUser->exp              = 0;
    pUser->level           = 1;
    pUser->winCount         = 0;
    pUser->loseCount        = 0;
    pUser->timeRemainGold   = 0;
    pUser->coins            = 0;
    pUser->bestScore       = 0;

}
static void ResetServerUserData(ServerUserData *pSUser) {
    ResetUserData(&pSUser->user);
    pSUser->roomId           = NO_ROOM;
    pSUser->seatId           = NO_SEAT;

}
static void ResetPlayerData(PlayerData *pPlayer) {
    ResetUserData(&pPlayer->user);
    pPlayer->online         = OFFLINE;
    pPlayer->cardIndexPlayer = NO_CARD;
    pPlayer->remainCardsPlayer = pPlayer->cardIndexPlayer;
    pPlayer->action         = PLAYER_ACTION_NONE;
    pPlayer->exp            = 0;
    pPlayer->level          = 1;
    pPlayer->winCount       = 0;
    pPlayer->loseCount      = 0;
    pPlayer->coins          = 0;
    pPlayer->winnerSeatId   = -1;
}
static void ResetServerRoomData(ServerRoomData *pSRoom) {
    pSRoom->gameId           = NO_GAME;
    pSRoom->token            = 0;
    memset(pSRoom->name, '\0', ROOM_NAME_LENGTH);
    memset(pSRoom->name, '\0', ROOM_PASS_LENGTH);
    pSRoom->minChip          = CHIP_STEP_DEFAULT;
    pSRoom->gameState        = GAME_STATE_WAIT;
    pSRoom->currentPlayer    = NO_SEAT;
    pSRoom->lastestUpdateId  = 0;
    pSRoom->playerCount      = 0;
    pSRoom->time             = 0;
    pSRoom->nextCardIndex    = 0;
    pSRoom->coinsRoom        = 0;
    pSRoom->roundDrawCard    = -1;
    pSRoom->randIndex        = 0;
    pSRoom->layout          = 0;
    pSRoom->tmpGold         = 0;
    pSRoom->tmpExp          = 0;    
    pSRoom->tmpLevel        = 0;
    pSRoom->tmpWinCount     = 0;
    pSRoom->tmpLoseCount    = 0;
    pSRoom->bestScore       = 0;

    /*--Reset Deck--*/
    int indexCard  = 0;

    for (s8 i = 0; i < MAX_CLUB; i++)
    {
        for (s8 j = 0; j < MAX_SUIT; j++) {
            pSRoom->cards[indexCard].id = indexCard;
            pSRoom->cards[indexCard].club = i;
            indexCard++;
        }
    }
    for (s8 seat = 0; seat < MAX_SEAT; seat++) ResetPlayerData(&pSRoom->players[seat]);
    
    for (s8 i = 0; i < MAX_CLUB; i++) {
        pSRoom->button[i] = TRUE;
        pSRoom->listenner[i]       = -1;
        pSRoom->btn_count[i]       = 0;
        pSRoom->statusCard[i]      = STATUS_DEFAULT;
    }

}
static s32 UpdateUserData(int dbIndex, UserData *pUser) {
    if (STSV_DBQuery(dbIndex, "SELECT * FROM users WHERE id = %d", pUser->userId)) {
        if (STSV_DBNumRow(dbIndex) == 1) {
            STSV_DBFetchRow(dbIndex);
            SetUserData(dbIndex, pUser); // update user in server
        }
        else {
            SLOG("[ERROR] [UpdateUserData] User (%d) not found\n", pUser->userId);
            return -1;
        }
    }
    else {
        SLOG("[ERROR] [UpdateUserData] Select query userId (%d) fail\n", pUser->userId);
        return -2;
    }
    return 0;
}
static void SetUserOffline(int dbIndex, u32 userId, int count) {
    if (count < 10) {
        if (!STSV_DBQuery(dbIndex, "UPDATE users SET online = 0 WHERE id = %d", userId)) {
            SLOG("[INFO] [SetUserOffline] Set User Offline Fail (%d) (userId: %d)\n", count, userId);
            SetUserOffline(dbIndex, userId, ++count);
        }
        else {
            SLOG("[INFO] [SetUserOffline] Set User Offline OK! (userId: %d)\n", userId);
        }
    }
    else {
        SLOG("[ERROR] [SetUserOffline] Can't Set Offline (userId: %d)\n", userId);
    }
}
static void LeaveRoom(ServerRoomData *pSRoom, ServerUserData *pSUser) {
    pSRoom->playerCount--;
    SLOG("[INFO] [LeaveRoom] Current player in room (%d) %d\n", pSRoom->roomId, pSRoom->playerCount);
    if (pSRoom->playerCount <= 0) {
        pSUser->roomId = NO_ROOM;
        pSUser->seatId = NO_SEAT;
        ResetServerRoomData(pSRoom);
        SLOG("[INFO] [LeaveRoom] Reset room (%d)\n", pSRoom->roomId);
    }
    else {
        SLOG("[INFO] [LeaveRoom] Player '%s' (%d) leave room '%s' (%d)\n", pSUser->user.name, pSUser->user.userId, pSRoom->name, pSRoom->roomId);
        ResetPlayerData(&pSRoom->players[pSUser->seatId]);
        
        pSUser->roomId           = NO_ROOM;
        pSUser->seatId           = NO_SEAT;
        pSRoom->lastestUpdateId++;
    }
}
static bool CheckName(RegisterRequest *req, RegisterResponse *res){
    if(strlen(req->name) < MIN_NAME_LENGTH){
        res->code = REGISTER_CODE_NAME_SHORT;
        SLOG("[ERROR] User (%s) send name(%s) length < MIN_NAME_LENGTH(client bug?)",req->email,req->name);
        return false;
    }
    return true;
}
static void LoginRequestHandler(Client_t *client, LoginRequest *req){
    LoginResponse res = { .header = LOGIN_HEADER, .code = LOGIN_CODE_ERROR, .latestGameId = NO_GAME, .latestRoomId = NO_ROOM, .latestSeatId = NO_SEAT };
    ServerUserData *pSUser = &users[client->idx];
    
    int dbIndex = STSV_DBFindAndLockResource();
    if (STSV_DBQuery(dbIndex, "SELECT * FROM users WHERE email = '%s' LIMIT 1", req->email)) {
        if (STSV_DBNumRow(dbIndex) == 1) {
            STSV_DBFetchRow(dbIndex);
            
            u8 status = atoi(STSV_DBGetRowStringValue(dbIndex, 16));
            if (status == OFFLINE) {
                SetUserData(dbIndex, &pSUser->user); // update user in server
                SLOG("pSUser->roomId(%d) pSUser->seatId(%d)",pSUser->roomId,pSUser->seatId);
                // lastgame data
                res.latestGameId    = atoi(STSV_DBGetRowStringValue(dbIndex, 13));
                res.latestRoomId    = atoi(STSV_DBGetRowStringValue(dbIndex, 14));
                res.latestSeatId    = atoi(STSV_DBGetRowStringValue(dbIndex, 15));
                SLOG("pSUser->roomId(%d) pSUser->seatId(%d)",pSUser->roomId,pSUser->seatId);
                SLOG("res.latestRoomId(%d) res.latestSeatId(%d)",res.latestRoomId,res.latestSeatId);
                if (STSV_DBQuery(dbIndex, "UPDATE users SET online = %d WHERE id = %d", ONLINE, pSUser->user.userId)) {
                    if (res.latestGameId != NO_GAME && res.latestRoomId != NO_ROOM && res.latestSeatId != NO_SEAT) {
                        if (rooms[res.latestRoomId].gameId == res.latestGameId) {
                            res.code = LOGIN_CODE_REJOIN_OK;
                            
                            // assign player to room
                            pSUser->roomId = res.latestRoomId;
                            pSUser->seatId = res.latestSeatId;
                            
                            ServerRoomData *room = &rooms[pSUser->roomId];
                            pthread_mutex_lock(&room->pull_mutex);
                            
                            memcpy(&room->players[pSUser->seatId].user, &pSUser->user, sizeof(UserData));
                            room->players[pSUser->seatId].online = ONLINE;
                            
                            pthread_mutex_unlock(&room->pull_mutex);
                            
                            SLOG("[INFO] [LoginRequestHandler] Player '%s' (%d) login success and rejoin game '%s' (%d) seat (%d)", pSUser->user.name, pSUser->user.userId, room->name, room->roomId, pSUser->seatId);
                        }
                        else {
                            res.code = LOGIN_CODE_REJOIN_END;
                            
                            // game was ended clear latest
                            if (STSV_DBQuery(dbIndex, "UPDATE users SET lastest_game_id = %d, lastest_room_id = %d, lastest_seat_id = %d WHERE id = %d", NO_GAME, NO_ROOM, NO_SEAT, pSUser->user.userId)) {
                                // latest game was clear
                            }
                            
                            SLOG("[INFO] [LoginRequestHandler] Player '%s' (%d) login success but can't rejoin game (%d) cuz game was end", pSUser->user.name, pSUser->user.userId, res.latestGameId);
                        }
                    }
                    else {
                        res.code = LOGIN_CODE_OK;
                        SLOG("[INFO] [LoginRequestHandler] Player '%s' (%d) login success\n", pSUser->user.name, pSUser->user.userId);
                    }
                }
            }
            else {
                res.code = LOGIN_CODE_ALREADY_LOGIN;
               
                SLOG("[WARN] [LoginRequestHandler] Player '%s' (%d) already online\n", pSUser->user.name, pSUser->user.userId);

            }
        }
        else {
            res.code = LOGIN_CODE_NEW_USER;
            SLOG("[INFO] [LoginRequestHandler] New player login email (%s)\n", req->email);
        }
        memcpy(&res.user, &pSUser->user, sizeof(UserData));
    }
    else {
        SLOG("[ERROR] [LoginRequestHandler] Select query email (%s) fail\n", req->email);
    }
    STSV_DBUnlockResource(dbIndex);
    
    STSV_SendData(client, client->socket, &res, sizeof(LoginResponse));
}
static void RegisterRequestHandler(Client_t *client, RegisterRequest *req) {
    RegisterResponse res = { .header = REGISTER_HEADER, .code = REGISTER_CODE_ERROR };
    
    if (CheckName(req, &res)) {
        pthread_mutex_lock(&register_mutex);
        int dbIndex = STSV_DBFindAndLockResource();
        if (STSV_DBQuery(dbIndex, "SELECT name FROM users WHERE name = '%s' GROUP BY name", req->name)) {
            if (STSV_DBQuery(dbIndex, "INSERT INTO users (uuid,email,name, character_id,created_at) VALUES ('%s','%s', '%s', %d, NOW())", req->uuid,req->email,req->name, req->characterId)) {
                res.code = REGISTER_CODE_OK;
                SLOG("[INFO] [RegisterRequestHandler] Player uuid (%s) email(%s) name (%s) characterId (%d) register successful\n", req->uuid,req->email,req->name,req->characterId);
            }
            else {
                SLOG("[ERROR] [RegisterRequestHandler] Insert query uuid (%s) name (%s) fail\n", req->uuid, req->name);
            }
        }
        else {
            SLOG("[ERROR] [RegisterRequestHandler] Select query name (%s) fail\n", req->name);
        }
        STSV_DBUnlockResource(dbIndex);

        int dbIndex1 = STSV_DBFindAndLockResource();
        if (STSV_DBQuery(dbIndex1, "INSERT INTO characters_slot (character_1,character_2,character_3,character_4,created_at) VALUES (%d,%d,%d,%d, NOW())",1,1,1,1)) {
        }
      
        STSV_DBUnlockResource(dbIndex1);
        pthread_mutex_unlock(&register_mutex);
    }
    
    STSV_SendData(client, client->socket, &res, sizeof(RegisterResponse));
    
    if (res.code == REGISTER_CODE_OK) {
        LoginRequest logReq = { .header = LOGIN_HEADER };
        strcpy(logReq.email, req->email);
        LoginRequestHandler(client, &logReq);
    }
}
static void QuickJoinRoomRequestHandler(Client_t *client, QuickJoinRoomRequest *req) {
    QuickJoinRoomResponse res = { .header = QUICK_JOIN_ROOM_HEADER, .code = QUICK_JOIN_ROOM_CODE_ERROR };
    ServerUserData *pSUser = &users[client->idx];    
    if (pSUser->user.userId != FREE_USER_SLOT) {
        
        if (pSUser->roomId == NO_ROOM && pSUser->seatId == NO_SEAT) {
            int dbIndex = STSV_DBFindAndLockResource();
            UpdateUserData(dbIndex, &pSUser->user);
            STSV_DBUnlockResource(dbIndex);
            
            u32 tmpGold = pSUser->user.gold;
            // find room
            for (s16 rId = 0; rId < MAX_ROOM; rId++) {
                ServerRoomData *pSRoom = &rooms[rId];
                pthread_mutex_lock(&pSRoom->room_mutex);
                if (pSRoom->playerCount >= MAX_SEAT) {
                    pthread_mutex_unlock(&pSRoom->room_mutex);
                    // room full
                    continue;
                }
                
                pthread_mutex_lock(&pSRoom->pull_mutex);
                if (pSRoom->gameState != GAME_STATE_WAIT) {
                    pthread_mutex_unlock(&pSRoom->pull_mutex);
                    // game started
                    continue;
                }
                // find seat
                for (s8 seat = 0; seat < MAX_SEAT; seat++) {
                    if (pSRoom->players[seat].user.userId == FREE_USER_SLOT) {
                        res.code = JOIN_ROOM_CODE_OK;
                        
                        if (pSRoom->playerCount == 0) {
                            strcpy(pSRoom->name, pSUser->user.name);
                            SLOG("[INFO] [QuickJoinRoomRequestHandler] Room (%d) was create name (%s)", pSRoom->roomId, pSRoom->name);
                        }
                        
                        pSRoom->playerCount++;
                        
                        // assign user to seat
                        pSRoom->players[seat].user = pSUser->user; // copy
                        pSRoom->players[seat].online = ONLINE;
                        //pSRoom->layout = rand() % WORLD_LAYOUT;
                        pSRoom->layout = 0;

                        pSUser->roomId = rId;
                        pSUser->seatId = seat;
                        
                        SLOG("[INFO] [QuickJoinRoomRequestHandler] Player '%s' (%d) join room '%s' (%d) seat (%d)\n", pSUser->user.name, pSUser->user.userId, pSRoom->name, pSUser->roomId, pSUser->seatId);
                        
                        PackGameRoomData(&res.gameRoom, pSUser, pSRoom, true);
                        
                        if (pSRoom->playerCount == MAX_SEAT) {
                            InitGame(pSRoom);
                        }
                        else {
                            SLOG("[INFO] [QuickJoinRoomRequestHandler] Room (%d) wait %d/%d player\n", pSRoom->roomId, MAX_SEAT - pSRoom->playerCount, MAX_SEAT);
                        }
                        
                        // update
                        pSRoom->lastestUpdateId++;
                        SLOG("Last: %d\n", pSRoom->lastestUpdateId);
                        break;
                    }
                }
                
                pthread_mutex_unlock(&pSRoom->pull_mutex);
                pthread_mutex_unlock(&pSRoom->room_mutex);
                
                if (pSUser->roomId != NO_ROOM && pSUser->seatId != NO_SEAT) {
                    break;
                }
                
            }
            // End Room Loop All Room Full
            if (pSUser->roomId == NO_ROOM && pSUser->seatId == NO_SEAT) res.code = QUICK_JOIN_ROOM_CODE_FULL;
        }
        else {
            SLOG("[ERROR] [QuickJoinRoomRequestHandler] Player '%s' (%d) already in room '%s' (%d) seat (%d)\n", pSUser->user.name, pSUser->user.userId, rooms[pSUser->roomId].name, pSUser->roomId, pSUser->seatId);
        }
    }
    else {
        SLOG("[ERROR] [QuickJoinRoomRequestHandler] UserData not found clientId (%d)\n", client->idx);
    }
    
    STSV_SendData(client, client->socket, &res, sizeof(QuickJoinRoomResponse));
}
static void LeaveRoomRequestHandler(Client_t *client, LeaveRoomRequest *req) {
    ServerUserData *pSUser = &users[client->idx];
    ServerRoomData *pSRoom = &rooms[pSUser->roomId];
    if (pSUser->user.userId != FREE_USER_SLOT) {
        if (pSUser->roomId != NO_ROOM && pSUser->seatId != NO_SEAT) {
            pthread_mutex_lock(&pSRoom->pull_mutex);
            if (pSRoom->gameState == GAME_STATE_WAIT) {
                pthread_mutex_lock(&pSRoom->room_mutex);
                LeaveRoom(pSRoom, pSUser);
                pthread_mutex_unlock(&pSRoom->room_mutex);
            }
            pthread_mutex_unlock(&pSRoom->pull_mutex);
        }
    }
}
static void PlayerActionValue(ServerUserData *pSUser,ServerRoomData *pSRoom,PlayerData *pPlayers,s8 code){
    pSRoom->button[code] = FALSE;
    pSRoom->btn_count[code]++;
    if(pSRoom->btn_count[code] == 1){
        pPlayers[pSUser->seatId].action = code;
        pSRoom->listenner[code]    = pPlayers[pSUser->seatId].user.userId;
    }else {
        pPlayers[pSUser->seatId].action = PLAYER_ACTION_NONE;
    }
    pSRoom->lastestUpdateId++;
}
static void PlayerGameActionRequestHandler(Client_t *client, PlayerGameActionRequest *req) {

    ServerUserData *pSUser = &users[client->idx];
    ServerRoomData *pSRoom = &rooms[pSUser->roomId];
    PlayerData *pPlayers = pSRoom->players;
    pSRoom->currentPlayer = pSUser->seatId;
    SLOG("current player: %d, user: %d, request: %d,\n", pPlayers[pSUser->seatId].user.userId, pSUser->user.userId, req->code);

    PlayerGameActionResponse res = { .header = PLAYER_GAME_ACTION_HEADER, .code = PLAYER_GAME_ACTION_CODE_ERROR };

    if (pPlayers[pSUser->seatId].user.userId == pSUser->user.userId) {
        pthread_mutex_lock(&pSRoom->pull_mutex);
        if (pSRoom->gameState == GAME_STATE_IN_MATCH) {

            switch (req->code) {
                case PLAYER_GAME_ACTION_CODE_FOLD:
                pPlayers[pSUser->seatId].action = PLAYER_ACTION_FOLD;
                pSRoom->lastestUpdateId++;
                res.code = PLAYER_GAME_ACTION_CODE_FOLD;
                break;
                case PLAYER_GAME_ACTION_CODE_VALUE_0:
                PlayerActionValue(pSUser,pSRoom,pPlayers,PLAYER_ACTION_VALUE_0);
                res.code = PLAYER_GAME_ACTION_CODE_VALUE_0;
                break;
                case PLAYER_GAME_ACTION_CODE_VALUE_1:
                PlayerActionValue(pSUser,pSRoom,pPlayers,PLAYER_ACTION_VALUE_1);
                res.code = PLAYER_GAME_ACTION_CODE_VALUE_1;
                break;
                case PLAYER_GAME_ACTION_CODE_VALUE_2:
                PlayerActionValue(pSUser,pSRoom,pPlayers,PLAYER_ACTION_VALUE_2);
                res.code = PLAYER_GAME_ACTION_CODE_VALUE_2;
                break;
                case PLAYER_GAME_ACTION_CODE_VALUE_3:
                PlayerActionValue(pSUser,pSRoom,pPlayers,PLAYER_ACTION_VALUE_3);
                res.code = PLAYER_GAME_ACTION_CODE_VALUE_3;
                break;
                case PLAYER_GAME_ACTION_CODE_VALUE_4:
                PlayerActionValue(pSUser,pSRoom,pPlayers,PLAYER_ACTION_VALUE_4);
                res.code = PLAYER_GAME_ACTION_CODE_VALUE_4;
                break;
                default:
                break;
            }

            STSV_SendData(client, client->socket, &res, sizeof(PlayerGameActionRequest));
        }
        else {
            SLOG("ERROR GAME STATE != IN MATCH\n");
        }
        pthread_mutex_unlock(&pSRoom->pull_mutex);
    }
    else {
        SLOG("ERROR Other UserId CALL\n");
    }
}
static void FetchDataRequestHandler(Client_t *client, FetchDataRequest *req){

    switch (req->code) {
        case FETCH_DATA_CODE_USERDATA: {
            FetchUserDataResponse res = { .header = FETCH_DATA_HEADER, .code = FETCH_DATA_CODE_USERDATA };
            UserData *user = &users[client->idx].user;
            if (user->userId != FREE_USER_SLOT) {
                int dbIndex = STSV_DBFindAndLockResource();
                UpdateUserData(dbIndex, user);
                STSV_DBUnlockResource(dbIndex);
                memcpy(&res.user, user, sizeof(UserData));
                STSV_SendData(client, client->socket, &res, sizeof(FetchUserDataResponse));
            }
            else {
                SLOG("FetchDataRequestHandler: FETCH_DATA_CODE_USERDATA -> FREE_USER_SLOT\n");
            }
        break;
        }
        default:
        break;
    }
}
static void PackGameRoomData(GameRoomData* pGameRoom, ServerUserData *pSUser, ServerRoomData *pSRoom, bool hideOtherCard) {
    pGameRoom->roomId = pSUser->roomId;
    pGameRoom->seatId = pSUser->seatId;
    pGameRoom->gameId = pSRoom->gameId;
    strcpy(pGameRoom->name, pSRoom->name);
    pGameRoom->minChip = pSRoom->minChip;
    pGameRoom->gameState  = pSRoom->gameState;
    pGameRoom->currentPlayer = pSRoom->currentPlayer;
    pGameRoom->lastCardIndex = pSRoom->cards[pSRoom->nextCardIndex];
    pGameRoom->remainCards = MAX_CARD - pSRoom->nextCardIndex - 1;
    pGameRoom->coinsRoom = pSRoom->coinsRoom;
    
    pGameRoom->layout     = pSRoom->layout;
    pGameRoom->lastestUpdateId = pSRoom->lastestUpdateId;

    for (s8 i = 0; i < MAX_CLUB; i++)
    {
        pGameRoom->button[i] = pSRoom->button[i];
        pGameRoom->listenner[i] = pSRoom->listenner[i];
        pGameRoom->statusCard[i] = pSRoom->statusCard[i];

    }
    for (s8 seat = 0; seat < MAX_SEAT; seat++) {
        if (pSRoom->players[seat].user.userId != FREE_USER_SLOT) {
            memcpy(&pGameRoom->players[seat].user, &pSRoom->players[seat].user, sizeof(UserData));
        }
        else {
            ResetUserData(&pGameRoom->players[seat].user);
        }
        pGameRoom->players[seat].online = pSRoom->players[seat].online;
        pGameRoom->players[seat].action = pSRoom->players[seat].action;
        pGameRoom->players[seat].remainCardsPlayer = pSRoom->players[seat].remainCardsPlayer;
        pGameRoom->players[seat].winnerSeatId = pSRoom->players[seat].winnerSeatId;
        pGameRoom->players[seat].coins = pSRoom->players[seat].coins;
        pGameRoom->players[seat].exp = pSRoom->players[seat].exp;


        for (int i = 0; i < MAX_CARD;i++)
        {
            pGameRoom->players[seat].card[i] = pSRoom->players[seat].card[i];
        }
    }
}
static u32 GetRoomTempRandom(ServerRoomData *pSRoom) {
    return pSRoom->tempRand[pSRoom->randIndex++];
}
static void GenerateRoomToken(char *token, s16 roomId) {
    static timeval tv;
    gettimeofday(&tv,NULL);
    sprintf(token,"%03d%ld%ld",roomId, tv.tv_sec, tv.tv_usec);
}
static void ChangeRoomState(ServerRoomData *pSRoom, s8 state, u32 time) {
    if (pSRoom->gameState != state) {
        pSRoom->time = time;
        pSRoom->gameState = state;
    }
}
static void ShuffleCards(ServerRoomData *pSRoom) {
    int i = MAX_CARD - 1;
    int j;
    Cards temp;

    while (i >= 0) {
        j = GetRoomTempRandom(pSRoom) % (i + 1);
        temp = pSRoom->cards[i];
        pSRoom->cards[i] = pSRoom->cards[j];
        pSRoom->cards[j] = temp;
        i = i - 1;
    }

    SLOG("ROOM : %d\t", pSRoom->roomId);
    for (int i = 0; i < MAX_CARD; i++) {
        SLOG("%d\t", pSRoom->cards[i].id);
        SLOG("%d\n ", pSRoom->cards[i].club);
    }
    SLOG("\n");
}

static Cards DrawCardFromDeck(ServerRoomData *pSRoom) {
    pSRoom->roundDrawCard++;
    if (pSRoom->nextCardIndex < MAX_CARD) {
        if(pSRoom->roundDrawCard > 0){
            return pSRoom->cards[pSRoom->nextCardIndex++];
        }else{
            return pSRoom->cards[pSRoom->nextCardIndex];
        }

    }
}

static void NextRound(ServerRoomData *pSRoom) {
    SLOG("NextRound fn\n");

        for (s8 i = 0; i < MAX_CLUB; i++){
            pSRoom->button[i] = true;
        }
        s8 offset = pSRoom->currentPlayer;
        for (s8 seat = 0; seat < MAX_SEAT; seat++) {
            pSRoom->players[offset].action = PLAYER_ACTION_NONE;
            offset = (offset + 1) % MAX_SEAT;
        }

        DrawCardFromDeck(pSRoom);    
        ChangeRoomState(pSRoom, GAME_STATE_FOLD_CARD, STOS_GetTickCount());
}

static void InitGame(ServerRoomData *pSRoom) {
    SLOG("\n");
    char token[TOKEN_LENGTH];
    GenerateRoomToken(token, pSRoom->roomId);   
    int dbIndex = STSV_DBFindAndLockResource();
    if (STSV_DBQuery(dbIndex, "INSERT INTO games (room_id, token, name, password, seat1_user_id, seat2_user_id, seat3_user_id, seat4_user_id) VALUES(%d, '%s', '%s', '%s', %d, %d, %d, %d)",
                     pSRoom->roomId, token,
                     pSRoom->name, pSRoom->pass,
                     pSRoom->players[0].user.userId,
                     pSRoom->players[1].user.userId,
                     pSRoom->players[2].user.userId,
                     pSRoom->players[3].user.userId))
    {
        pSRoom->gameId = STSV_DBLastInsert(dbIndex);
        pSRoom->token = atoi(token);
        pSRoom->currentPlayer = NO_SEAT;
        ChangeRoomState(pSRoom, GAME_STATE_LOADING, STOS_GetTickCount());
    }
    else {
        ChangeRoomState(pSRoom, GAME_STATE_ERROR, 0);
    }

    STSV_DBUnlockResource(dbIndex);
}
static void SendGamePullingResponse(u32 last, Client_t *client, ServerUserData *sUser, ServerRoomData *room, bool hide) {
    if (last == room->lastestUpdateId) {
        GamePullingLatestGameResponse res = { .header = GAME_PULLING_HEADER, .code = GAME_PULLING_CODE_LATEST };
        STSV_SendData(client, client->socket, &res, sizeof(GamePullingLatestGameResponse));
    }
    else {
        GamePullingUpdateGameResponse res = { .header = GAME_PULLING_HEADER, .code = GAME_PULLING_CODE_UPDATE };
        PackGameRoomData(&res.gameRoom, sUser, room, hide);

        SLOG("Last: %d\n", res.gameRoom.lastestUpdateId);
        STSV_SendData(client, client->socket, &res, sizeof(GamePullingUpdateGameResponse));
    }
}
static void InMatch(ServerUserData *pSUser) {

    ServerRoomData *pSRoom = &rooms[pSUser->roomId];
    PlayerData *pPlayers = pSRoom->players;
    if (pSRoom->gameState == GAME_STATE_IN_MATCH){
       
        if(pPlayers[pSUser->seatId].action != PLAYER_ACTION_NONE && pPlayers[pSUser->seatId].action != PLAYER_ACTION_FOLD){

            if (pPlayers[pSUser->seatId].action == pSRoom->cards[pSRoom->nextCardIndex].club){ 
                pSRoom->coinsRoom += 100;
                pSRoom->statusCard[pPlayers[pSUser->seatId].action] = STATUS_TRUE;
                pPlayers[pSUser->seatId].cardIndexPlayer += 2;
                pPlayers[pSUser->seatId].remainCardsPlayer = pPlayers[pSUser->seatId].cardIndexPlayer;
                
                for (int i = 0; i < MAX_SEAT; i++)
                {
                    if (pPlayers[i].user.userId != pPlayers[pSUser->seatId].user.userId && pPlayers[i].remainCardsPlayer > 0) {
                        pPlayers[i].cardIndexPlayer--;
                        pPlayers[i].remainCardsPlayer = pPlayers[i].cardIndexPlayer;
                        SLOG("pPlayers(%d) [InMatch]remainCardsPlayer(%d)\n",pPlayers[i].user.userId,pPlayers[i].remainCardsPlayer); 

                    }
                }
                pSRoom->lastestUpdateId++;
                SLOG("currentPlayer(%d) [InMatch]remainCardsPlayer(%d)\n",pSRoom->currentPlayer,pPlayers[pSUser->seatId].remainCardsPlayer); 

                ChangeRoomState(pSRoom, GAME_STATE_CALCULATE_MATCH_RESULT, STOS_GetTickCount());

            }
            else{
                if (pPlayers[pSUser->seatId].remainCardsPlayer > 0){
                    pPlayers[pSUser->seatId].cardIndexPlayer--;
                    pPlayers[pSUser->seatId].remainCardsPlayer = pPlayers[pSUser->seatId].cardIndexPlayer;
                }
                pSRoom->statusCard[pPlayers[pSUser->seatId].action] = STATUS_FALSE;

                SLOG("currentPlayer(%d) [InMatch]remainCardsPlayer(%d)\n",pSRoom->currentPlayer,pPlayers[pSUser->seatId].remainCardsPlayer);
                pPlayers[pSUser->seatId].action = PLAYER_ACTION_FOLD;
            }
        }
    pSRoom->lastestUpdateId++;
    }
}

static void SwapPlayers(PlayerData *pPlayers){
    s8 seat,d;
    PlayerData swap;
    for (seat = 0; seat < (MAX_SEAT - 1); seat++){   

        for (d = 0; d < MAX_SEAT - seat - 1; d++)
        {
            if (pPlayers[d].remainCardsPlayer > pPlayers[d+1].remainCardsPlayer)
            {
                swap            = pPlayers[d];
                pPlayers[d]     = pPlayers[d+1];
                pPlayers[d+1]   = swap;
            }
        }
    }
}

static void GamePullingRequestHandler(Client_t *client, GamePullingGameRequest *req) {
    ServerUserData *pSUser = &users[client->idx];

    if (pSUser->roomId == NO_ROOM) {
        SLOG("NO ROOM !!\n");
        GamePullingUpdateGameResponse pr = { .header = GAME_PULLING_HEADER, .code = GAME_PULLING_CODE_UPDATE };
        pr.gameRoom.roomId = NO_ROOM;
        pr.gameRoom.seatId = NO_SEAT;
        pr.gameRoom.lastestUpdateId = STOS_GetTickCount();

        SLOG("NO ROOM !! %d %d %d\n", pr.gameRoom.roomId, pr.gameRoom.seatId, pr.gameRoom.lastestUpdateId);
        STSV_SendData(client, client->socket, &pr, sizeof(GamePullingUpdateGameResponse));
        return;
    }

    ServerRoomData *pSRoom = &rooms[pSUser->roomId];
    PlayerData *pPlayers = pSRoom->players;

    pPlayers[pSUser->seatId].online = ONLINE;
    switch (pSRoom->gameState) {
        case GAME_STATE_WAIT:
        SLOG("[GAME_STATE_WAIT]\n");
        SendGamePullingResponse(req->lastestUpdateId, client, pSUser, pSRoom, true);
        break;
        case GAME_STATE_LOADING:
        SLOG("[GAME_STATE_LOADING]\n");
        if (STOS_GetTickCount() - pSRoom->time > TIME_LOGDING) {
            pthread_mutex_lock(&pSRoom->pull_mutex);
            if (pSRoom->gameState == GAME_STATE_LOADING) {
                ChangeRoomState(pSRoom, GAME_STATE_INIT_MATCH, STOS_GetTickCount());
                pSRoom->lastestUpdateId++;
            }
        }
        pthread_mutex_unlock(&pSRoom->pull_mutex);
        SendGamePullingResponse(req->lastestUpdateId, client, pSUser, pSRoom, true);
        break;
        case GAME_STATE_INIT_MATCH:
        SLOG("[GAME_STATE_INIT_MATCH]\n");
        if (STOS_GetTickCount() - pSRoom->time > TIME_INIT_MATCH) {
            pthread_mutex_lock(&pSRoom->pull_mutex);
            if (pSRoom->gameState == GAME_STATE_INIT_MATCH) {
                srand(pSRoom->token);
                for (s8 index = 0; index < MAX_TEMP_RANDOM; index++) {
                    pSRoom->tempRand[index] = rand();
                }
                pSRoom->randIndex = 0;
                pSRoom->currentPlayer = 0;
                pSRoom->nextCardIndex= 0;
                ShuffleCards(pSRoom);
                NextRound(pSRoom);
                pSRoom->lastestUpdateId++;   
            }
        }
        pthread_mutex_unlock(&pSRoom->pull_mutex);
        SendGamePullingResponse(req->lastestUpdateId, client, pSUser, pSRoom, true);
        break;
        case GAME_STATE_FOLD_CARD:
        SLOG("[GAME_STATE_FOLD_CARD]\n");
        if (STOS_GetTickCount() - pSRoom->time > TIME_FOLD_CARD) {
            pthread_mutex_lock(&pSRoom->pull_mutex);
            if (pSRoom->gameState == GAME_STATE_FOLD_CARD) {
                for (s8 i = 0; i < MAX_CLUB; ++i) {
                    pSRoom->btn_count[i] = 0;
                    pSRoom->statusCard[i] = 0;
                }
                
                ChangeRoomState(pSRoom, GAME_STATE_IN_MATCH, STOS_GetTickCount());
                pSRoom->lastestUpdateId++;
            }
            pthread_mutex_unlock(&pSRoom->pull_mutex);
        }
        SendGamePullingResponse(req->lastestUpdateId, client, pSUser, pSRoom, true);
        break;
        case GAME_STATE_IN_MATCH:
        SLOG("[GAME_STATE_IN_MATCH]\n");
        InMatch(pSUser); 
        if (STOS_GetTickCount() - pSRoom->time > TIME_IN_GAME) {
            pthread_mutex_lock(&pSRoom->pull_mutex);
            if (pSRoom->gameState == GAME_STATE_IN_MATCH) {
                ChangeRoomState(pSRoom, GAME_STATE_CALCULATE_MATCH_RESULT, STOS_GetTickCount());
                pSRoom->lastestUpdateId++;
            }
            pthread_mutex_unlock(&pSRoom->pull_mutex);
        }
        SendGamePullingResponse(req->lastestUpdateId, client, pSUser, pSRoom, true);
        break;
        case GAME_STATE_CALCULATE_MATCH_RESULT:
        SLOG("[GAME_STATE_CALCULATE_MATCH_RESULT]\n");
        if (STOS_GetTickCount() - pSRoom->time > TIME_CALCULATE) {
            pthread_mutex_lock(&pSRoom->pull_mutex);
            if (pSRoom->gameState == GAME_STATE_CALCULATE_MATCH_RESULT) {
                if (pSRoom->nextCardIndex +1 < (MAX_CARD - 60)) {
                NextRound(pSRoom);
                }else{

                ChangeRoomState(pSRoom, GAME_STATE_POST_MATCH, STOS_GetTickCount());
                }
            pSRoom->lastestUpdateId++;
            }
            pthread_mutex_unlock(&pSRoom->pull_mutex);
        }
        SendGamePullingResponse(req->lastestUpdateId, client, pSUser, pSRoom, true);
        break;
        case GAME_STATE_POST_MATCH:
        SLOG("[GAME_STATE_POST_MATCH]\n");
        pthread_mutex_lock(&pSRoom->pull_mutex);
        if (pSRoom->gameState == GAME_STATE_POST_MATCH) {          
            s8 seat,n,max;
            SwapPlayers(pPlayers);
            n = 1;
            max = pPlayers[0].remainCardsPlayer;
            for (seat = 0; seat < MAX_SEAT; seat++){ 
                pPlayers[seat].winnerSeatId = seat;
                if (pPlayers[seat].remainCardsPlayer == max)
                {   
                    pPlayers[seat].coins = (pSRoom->coinsRoom * pPlayers[seat].remainCardsPlayer * n);
                    pPlayers[seat].exp = (100 * pPlayers[seat].remainCardsPlayer * n);
                    max = pPlayers[seat].remainCardsPlayer;
                   
                }else{
                    n += 1;
                    pPlayers[seat].coins = (pSRoom->coinsRoom * pPlayers[seat].remainCardsPlayer * n);
                    pPlayers[seat].exp = (100 * pPlayers[seat].remainCardsPlayer * n);
                    max = pPlayers[seat].remainCardsPlayer;
                    
                }
                SLOG("no:(%d) pPlayers:(%d) remainCards:(%d) coin:(%d) exp:(%d)\n",seat,pPlayers[seat].user.userId,pPlayers[seat].remainCardsPlayer,pPlayers[seat].coins,pPlayers[seat].exp);
            }

            for (seat = 0; seat < MAX_SEAT; seat++)
            {
                if (pPlayers[seat].remainCardsPlayer == max && pPlayers[seat].remainCardsPlayer != 0){
                        pPlayers[seat].winCount = 1;
                }else{
                        pPlayers[seat].loseCount = 1;
                }
                SLOG("(%d)(%d)\n",pPlayers[seat].user.userId,pPlayers[seat].winCount);

                int dbIndex = STSV_DBFindAndLockResource();
                pSRoom->tmpGold = pSRoom->players[seat].user.gold;
                pSRoom->tmpExp  = pSRoom->players[seat].user.exp;
                pSRoom->tmpLevel = pSRoom->players[seat].user.level;
                pSRoom->tmpWinCount = pSRoom->players[seat].user.winCount;
                pSRoom->tmpLoseCount = pSRoom->players[seat].user.loseCount;
                pSRoom->bestScore = pSRoom->players[seat].user.bestScore;
                
                //--Calculate--//
                pSRoom->tmpGold += pPlayers[seat].coins;
                pSRoom->tmpExp += pPlayers[seat].exp;
                pSRoom->tmpWinCount += pPlayers[seat].winCount;
                pSRoom->tmpLoseCount += pPlayers[seat].loseCount;
                
                if (pSRoom->tmpExp >= (LEVELUP * pSRoom->tmpLevel)){ 
                    pSRoom->tmpLevel += 1;
                    pSRoom->tmpExp = 0; 
                    SLOG("Level Up!\n");
                } 
                if (pSRoom->bestScore < pPlayers[seat].coins) pSRoom->bestScore = pPlayers[seat].coins;

                if (STSV_DBQuery(dbIndex, "UPDATE users SET gold=%d, exp=%d, level=%d, win_count=%d, lose_count=%d, best_score=%d WHERE id = %d",
                pSRoom->tmpGold,pSRoom->tmpExp,pSRoom->tmpLevel,pSRoom->tmpWinCount,pSRoom->tmpLoseCount,pSRoom->bestScore,pPlayers[seat].user.userId)){}


                STSV_DBUnlockResource(dbIndex);
                ChangeRoomState(pSRoom, GAME_STATE_SHOW, STOS_GetTickCount());
                pSRoom->lastestUpdateId++;
            }
        }
            pthread_mutex_unlock(&pSRoom->pull_mutex);
        SendGamePullingResponse(req->lastestUpdateId, client, pSUser, pSRoom, true);
        break;
        case GAME_STATE_SHOW:
        SLOG("[GAME_STATE_SHOW]\n");
        if (STOS_GetTickCount() - pSRoom->time > TIME_POST_MATCH) {
            pthread_mutex_lock(&pSRoom->pull_mutex);
            if (pSRoom->gameState == GAME_STATE_SHOW) {
                UserData *user = &users[client->idx].user;
                        // game end
        int dbIndex = STSV_DBFindAndLockResource();
        
        if (STSV_DBQuery(dbIndex, "UPDATE users SET lastest_game_id = %d, lastest_room_id = %d, lastest_seat_id = %d WHERE lastest_game_id = %d", NO_GAME, NO_ROOM, NO_SEAT, pSRoom->gameId)) {
            // latest game was clear
        }
                STSV_DBUnlockResource(dbIndex);

                ChangeRoomState(pSRoom, GAME_STATE_END_GAME, STOS_GetTickCount());
                pSRoom->lastestUpdateId++;
            }
            pthread_mutex_unlock(&pSRoom->pull_mutex);
        }
        SendGamePullingResponse(req->lastestUpdateId, client, pSUser, pSRoom, true);
        break;
        case GAME_STATE_END_GAME:
        SLOG("[GAME_STATE_END_GAME]\n");
        if (STOS_GetTickCount() - pSRoom->time > TIME_FORCE_RESET_ROOM) {

            pSUser->roomId = NO_ROOM;
            pSUser->seatId = NO_SEAT;
            pSRoom->lastestUpdateId++;
            pSRoom->time = STOS_GetTickCount();

            SendGamePullingResponse(req->lastestUpdateId, client, pSUser, pSRoom, true);
        }
        break;
        case GAME_STATE_ERROR:
        break;
        default:
        break;
    }
}

static void LobbyActionRequestHandle(Client_t *client, LobbyActionRequest *req) {
    LobbyActionRankingResponse res;
    SLOG("LOBBY_ACTION_CODE_TOTAL_USERS!\n");

    switch (req->code) {
        case LOBBY_ACTION_CODE_TOTAL_USERS:{
            SLOG("LOBBY_ACTION_CODE_TOTAL_USERS!\n");
            res.header = LOBBY_ACTION_HEADER;
            res.code = LOBBY_ACTION_CODE_TOTAL_USERS;
            int dbIndex = STSV_DBFindAndLockResource();

            if (STSV_DBQuery(dbIndex, "SELECT COUNT(*) FROM users"))
            {
             if (STSV_DBNumRow(dbIndex) == 1) {
                STSV_DBFetchRow(dbIndex);
                res.total_users = atoi(STSV_DBGetRowStringValue(dbIndex, 0));
                SLOG("counting users(%d)\n",res.total_users);
            }
            }
            STSV_DBUnlockResource(dbIndex);
            STSV_SendData(client, client->socket, &res, sizeof(LobbyActionRankingResponse));

        break;
        }
        case LOBBY_ACTION_CODE_RANKING: {
            SLOG("LOBBY_ACTION_CODE_RANKING!\n");

            res.header = LOBBY_ACTION_HEADER;
            res.code = LOBBY_ACTION_CODE_RANKING;
            int dbIndex = STSV_DBFindAndLockResource();
            int dbIndex1 = STSV_DBFindAndLockResource();
            if (STSV_DBQuery(dbIndex, "SELECT * FROM users ORDER BY best_score DESC ")){
            int numRow = STSV_DBNumRow(dbIndex);
                if (numRow > 0) {
                    for (int row = 0; row < numRow; row++) {
                        STSV_DBFetchRow(dbIndex);
                        u32 userId   = atoi(STSV_DBGetRowStringValue(dbIndex, 0));
                        if (STSV_DBQuery(dbIndex1, "UPDATE users SET rank_id = %d WHERE id = %d",
                        row+1 ,userId)){}
                    }

                }
            }
            STSV_DBUnlockResource(dbIndex);
            STSV_DBUnlockResource(dbIndex1);

            int dbIndex2 = STSV_DBFindAndLockResource();
            if (STSV_DBQuery(dbIndex2, "SELECT * FROM users WHERE rank_id = %d", req->id)) {

                if (STSV_DBNumRow(dbIndex2) == 1) {
                    STSV_DBFetchRow(dbIndex2);
                    strcpy(res.uuid, STSV_DBGetRowStringValue(dbIndex2,1));
                    strcpy(res.email, STSV_DBGetRowStringValue(dbIndex2,2));
                    strcpy(res.name, STSV_DBGetRowStringValue(dbIndex2,3));
                    res.rankId   = atoi(STSV_DBGetRowStringValue(dbIndex2, 4));
                    res.characterId = atoi(STSV_DBGetRowStringValue(dbIndex2, 5)); // avatar
                    res.level       = atoi(STSV_DBGetRowStringValue(dbIndex2, 8));
                    res.bestScore   = atoi(STSV_DBGetRowStringValue(dbIndex2, 11));
                }else{
                    SLOG("[EMPTY] [UserData] User (%d) not found\n", req->id);
                    strcpy(res.uuid, "");
                    strcpy(res.email,"");
                    res.rankId = 0;
                    strcpy(res.name,"");
                    res.characterId = 1;
                    res.level     = 0;
                    res.bestScore = 0;
                }
            }
            STSV_DBUnlockResource(dbIndex2);

           STSV_SendData(client, client->socket, &res, sizeof(LobbyActionRankingResponse));
           break;
        }
        default:
        break;
    }
}


static void ShopActionRequestHandle(Client_t *client, ShopActionRequest *req) {
    ShopActionResponse res;
    ServerUserData *pSUser = &users[client->idx];
 
    switch (req->code) {
        case SHOP_ACTION_CODE:{
            res.header = SHOP_ACTION_HEADER;
            res.code = SHOP_ACTION_CODE;
            res.character_id     = req->character_id;
            SLOG("SHOP!\n");            //Unlocked level
            SLOG("req->character_id(%d)\n",req->character_id);
            
            int dbIndex = STSV_DBFindAndLockResource();
            if (STSV_DBQuery(dbIndex, "SELECT level, gold FROM characters WHERE id = %d", req->character_id)) {
                if (STSV_DBNumRow(dbIndex) == 1) {
                    STSV_DBFetchRow(dbIndex);
                s8 level   = atoi(STSV_DBGetRowStringValue(dbIndex, 0));
                u32 gold   = atoi(STSV_DBGetRowStringValue(dbIndex, 1));
                res.level_status = TRUE ? pSUser->user.level >= level : res.level_status = FALSE;
                res.gold_status  = TRUE ? pSUser->user.gold >= gold : res.gold_status = FALSE;
                SLOG("level(%d)\n",level);
                SLOG("gold(%d)\n",gold); 
                }
                SLOG("res.level_status(%d)\n",res.level_status);
                SLOG("res.gold_status(%d)\n",res.gold_status);
            }
            int dbIndex1 = STSV_DBFindAndLockResource();
            if (STSV_DBQuery(dbIndex1, "SELECT character_%d FROM characters_slot WHERE user_id = %d", req->character_id,pSUser->user.userId)) {

                if (STSV_DBNumRow(dbIndex1) == 1) {
                    STSV_DBFetchRow(dbIndex1);
                s8 character_status   = atoi(STSV_DBGetRowStringValue(dbIndex1, 0));
                res.character_status = character_status;
                SLOG("character_status(%d)\n",character_status); 
                }
            }
            SLOG("res.character_status(%d)\n",res.character_status);

            STSV_DBUnlockResource(dbIndex);
            STSV_DBUnlockResource(dbIndex1);
            STSV_SendData(client, client->socket, &res, sizeof(ShopActionResponse));

        break;
        }
        case SHOP_ACTION_CODE_CHARACTER:{
            res.header = SHOP_ACTION_HEADER;
            res.code = SHOP_ACTION_CODE_CHARACTER;
            int dbIndex = STSV_DBFindAndLockResource();
            if (STSV_DBQuery(dbIndex, "UPDATE users SET character_id = %d WHERE id = %d",req->character_id ,pSUser->user.userId)){
                 SLOG("UPDATE users SET character_id = %d",req->character_id);
            }
            STSV_DBUnlockResource(dbIndex);
            STSV_SendData(client, client->socket, &res, sizeof(ShopActionResponse));
        break;
        }
        default:
        break;
    }
}

void InitGameServer() {
    rooms = (ServerRoomData*)malloc(sizeof(ServerRoomData) * MAX_ROOM);
    users = (ServerUserData*)malloc(sizeof(ServerUserData) * MAX_PLAYER);
    pthread_mutex_init(&register_mutex, NULL);

    char token[TOKEN_LENGTH];
    s16 index = 0;
    for (index = 0; index < MAX_ROOM; index++) {
        ServerRoomData init = { .roomId = index };
        GenerateRoomToken(token, index);
        srand(atoi(token));

        /*--MakeDeck--*/
        int indexCard  = 0;
        for (s8 i = 0; i < MAX_CLUB; i++)
        {
            for (s8 j = 0; j < MAX_SUIT; j++) {

                rooms->cards[indexCard].id = indexCard;
                rooms->cards[indexCard].club = i;
                indexCard++;
            }
        }
        SLOG("ROOM : %d\n", index);
        for (int i = 0; i < MAX_CARD; i++) {
            SLOG("id%d\t\t", rooms->cards[i].id);
            SLOG("club%d\n",  rooms->cards[i].club);
        }
        SLOG("\n");

        ResetServerRoomData(&init);

        pthread_mutex_init(&init.pull_mutex, NULL);
        pthread_mutex_init(&init.room_mutex, NULL);
        memcpy(&rooms[index], &init, sizeof(ServerRoomData));
    }

    for (index = 0; index < MAX_PLAYER; index++) {
        ResetServerUserData(&users[index]);
    }


    int dbIndex = STSV_DBFindAndLockResource();
    STSV_DBQuery(dbIndex, "UPDATE users SET online = 0");
    STSV_DBUnlockResource(dbIndex);
}

void ClientConnect(Client_t* client) {
    SLOG("[INFO] ClientConnect: client idx (%d) connected\n", client->idx);
}
void ClientRequest(Client_t* client) {
    int data_count = STSV_GetDataCount(client->idx);
    u32 buffer_size;
    for (int i = 0; i < data_count; i++) {
        char* buffer = STSV_GetData(client->idx, i, &buffer_size);
        ProcessRequest(client, buffer, buffer_size);
    }
}
void ClientDisconnect(Client_t* client){

    ServerUserData *pSUser = &users[client->idx];

    if (pSUser->roomId != NO_ROOM && pSUser->seatId != NO_SEAT) {
        ServerRoomData *pSRoom = &rooms[pSUser->roomId];
        pthread_mutex_lock(&pSRoom->pull_mutex);
        if (pSRoom->gameState != GAME_STATE_WAIT) {
            SLOG("[INFO] [ClientDisconnect] Player '%s' (%d) leave game '%s' (%d)", pSUser->user.name, pSUser->user.userId, pSRoom->name, pSRoom->gameId);

            pSRoom->players[pSUser->seatId].online = OFFLINE;
            s8 count = 0;
            for (count = 0; count < MAX_SEAT; count++) {
                if (pSRoom->players[count].online == ONLINE) {
                    break;
                }
            }

            if (count == MAX_SEAT) {
                SLOG("[INFO] [ClientDisconnect] All player leave game '%s' (%d)\n", pSRoom->name, pSRoom->gameId);
                ResetServerRoomData(pSRoom);
            }
        }
        else {
            SLOG("[INFO] [ClientDisconnect] Player '%s' (%d) leave room '%s' (%d)\n", pSUser->user.name, pSUser->user.userId, pSRoom->name, pSRoom->roomId);

            pthread_mutex_lock(&pSRoom->room_mutex);
            LeaveRoom(pSRoom, pSUser);
            pthread_mutex_unlock(&pSRoom->room_mutex);
        }
    pSRoom->lastestUpdateId++; // update
    pthread_mutex_unlock(&pSRoom->pull_mutex);
}

int dbIndex = STSV_DBFindAndLockResource();
SetUserOffline(dbIndex, pSUser->user.userId, 0);
STSV_DBUnlockResource(dbIndex);

ResetServerUserData(pSUser);
}

void DestroyGameServer() {
    SLOG("[INFO] DestroyGameServer: server destroyed bye~\n");

}
void ProcessRequest(Client_t* client, char* buffer, u32 buffer_size) {
    HEADER *header = (HEADER*)buffer;
    switch (*header) {
        case LOGIN_HEADER:
            LoginRequestHandler(client, (LoginRequest*)buffer);
        break;
        case REGISTER_HEADER:           // 1001
            RegisterRequestHandler(client, (RegisterRequest*)buffer);
        break;
        case QUICK_JOIN_ROOM_HEADER:    // 2002
            QuickJoinRoomRequestHandler(client, (QuickJoinRoomRequest*)buffer);
        break;
        case LEAVE_ROOM_HEADER:         // 2003
            LeaveRoomRequestHandler(client, (LeaveRoomRequest*)buffer);
        break;
        case PLAYER_GAME_ACTION_HEADER: // 5000
            PlayerGameActionRequestHandler(client, (PlayerGameActionRequest*)buffer);
        break;
        case GAME_PULLING_HEADER:       // 5001
            GamePullingRequestHandler(client, (GamePullingGameRequest*)buffer);
        break;
        case FETCH_DATA_HEADER:         // 6000
            FetchDataRequestHandler(client, (FetchDataRequest*)buffer);
        break;
        case LOBBY_ACTION_HEADER:       // 7000
            LobbyActionRequestHandle(client, (LobbyActionRequest*)buffer);
            break;
        case SHOP_ACTION_HEADER:       // 8000
            ShopActionRequestHandle(client, (ShopActionRequest*)buffer);
            break;
        default:
            SLOG("[ERROR] [ProcessRequest] Unknow request header %d\n", *header);
        break;
}
}