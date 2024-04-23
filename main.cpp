
#include <array>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <random>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

#ifdef unix
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#include <utility>

// void clear_screen() { system("clear"); }

int kbhit(void) {
  struct termios oldt, newt;
  int ch;
  int oldf;

  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;

  newt.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);

  oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
  fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

  ch = getchar();

  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  fcntl(STDIN_FILENO, F_SETFL, oldf);

  if (ch != EOF) {
    ungetc(ch, stdin);
    return 1;
  }

  return 0;
}

#else
#ifdef WIN32
#include <Windows.h>

// void clear_screen() { system("cls"); }

#endif
#endif

enum DIRECTION { UP = 0, DOWN = 1, LEFT = 2, RIGHT = 3 };
enum ENEMY_TYPE { BLINKY = 0, PINKY = 1, INKY = 2, CLYDE = 3 };
enum ENEMY_MODE { SCATTER = 0, NORMAL = 1, FRIGHTENED = 2 };

std::random_device rd;
std::mt19937 gen(rd());

struct player {
  std::pair<size_t, size_t> pos;
  std::array<std::pair<char, char>, 4> icons = {
      std::pair{'v', 'o'}, {'^', 'o'}, {'>', 'o'}, {'<', 'o'}};
  DIRECTION direction;
  int current_anim_frame = 1;
  int score = 0, max_score = 0;
  bool is_over = false;

  std::pair<size_t, size_t> portal_1, portal_2;

  static bool has_no_collision(char c) {
    return std::string(" .@[]BPIC").find(c) != std::string::npos;
  }

  template <size_t width, size_t height>
  void move(const std::array<std::array<char, width>, height> &game_map) {
    switch (this->direction) {
    case UP:
      if (this->has_no_collision(
              game_map[this->pos.first - 1][this->pos.second]))
        this->pos.first--;
      break;
    case DOWN:
      if (this->has_no_collision(
              game_map[this->pos.first + 1][this->pos.second]))
        this->pos.first++;
      break;
    case LEFT:
      if (this->has_no_collision(
              game_map[this->pos.first][this->pos.second - 1]))
        this->pos.second--;
      break;
    case RIGHT:
      if (this->has_no_collision(
              game_map[this->pos.first][this->pos.second + 1]))
        this->pos.second++;
      break;
    default:
      throw std::invalid_argument("Unknown direction");
      break;
    }
  }
};

struct enemy {
  // references: https://gameinternals.com/understanding-pac-man-ghost-behavior

  std::pair<size_t, size_t> pos;
  char icon;
  bool moved = false;
  ENEMY_TYPE character;
  ENEMY_MODE mode;
  DIRECTION prev_move; // ghosts may never choose to reverse their direction of
                       // travel.
  std::pair<size_t, size_t> target;

  void set_character(ENEMY_TYPE type) {
    this->character = type;
    switch (this->character) {
    case BLINKY:
      this->icon = 'B';
      break;
    case PINKY:
      this->icon = 'P';
      break;
    case INKY:
      this->icon = 'I';
      break;
    case CLYDE:
      this->icon = 'C';
      break;
    default:
      break;
    }
  }

  // Define a function to calculate the Manhattan distance between two points
  size_t manhattanDistance(std::pair<size_t, size_t> p1,
                           std::pair<size_t, size_t> p2) {
    return std::abs((int)p1.first - (int)p2.first) +
           std::abs((int)p1.second - (int)p2.second);
  }

  void calculate_target(size_t width, size_t height, const player &pacman,
                        const enemy &blinky) {

    // The ghosts are always in one of three possible modes: Chase, Scatter, or
    // Frightened.

    switch (this->mode) {
    case SCATTER:
      /*
        In Scatter mode, each ghost has a fixed target tile, each of which is
        located just outside a different corner of the maze. This causes the
        four ghosts to disperse to the corners whenever they are in this mode.
      */

      switch (this->character) {
      case BLINKY:
        this->target = {1, width - 2};
        break;
      case PINKY:
        this->target = {1, 1};
        break;
      case INKY:
        this->target = {width - 2, height - 2};
        break;
      case CLYDE:
        this->target = {height - 2, 1};
        break;
      default:
        break;
      }

      break;
    case NORMAL:
      switch (this->character) {
      case BLINKY:
        /*
          Blinky's target tile in Chase mode is defined as Pac-Man's current
          tile. This ensures that Blinky almost always follows directly behind
          Pac-Man, unless the short-sighted decision-making causes him to take
          an inefficient path.
        */
        this->target = pacman.pos;
        break;
      case PINKY:
        /*
        Pinky's target tile in Chase mode is determined by looking at Pac-Man's
        current position and orientation, and selecting the location four tiles
        straight ahead of Pac-Man. At least, this was the intention, and it
        works when Pac-Man is facing to the left, down, or right, but when
        Pac-Man is facing upwards, an overflow error in the game's code causes
        Pinky's target tile to actually be set as four tiles ahead of Pac-Man
        and four tiles to the left of him. I don't want to frighten off
        non-programmers, but if you're interested in the technical details
        behind this bug, Don Hodges has written a great explanation, including
        the actual assembly code for Pinky's targeting, as well as a fixed
        version.
        */

        switch (pacman.direction) {
        case UP:
          // Pac-Man is facing upwards
          // Set Pinky's target tile four tiles ahead and four tiles to the
          // left of Pac-Man
          this->target.first = pacman.pos.first - 4;
          this->target.second = pacman.pos.second - 4;
          break;

          // Pac-Man is facing left, down, or right
          // Set Pinky's target tile four tiles straight ahead of Pac-Man
        case LEFT:
          this->target.first = pacman.pos.first - 4;
          this->target.second = pacman.pos.second;
          break;
        case DOWN:
          this->target.first = pacman.pos.first;
          this->target.second = pacman.pos.second + 4;
          break;
        case RIGHT:
          this->target.first = pacman.pos.first + 4;
          this->target.second = pacman.pos.second;
          break;
        }
        break;
      case INKY: {
        /*
         Inky is difficult to predict, because he is the only one of the ghosts
         that uses a factor other than Pac-Man's position/orientation when
         determining his target tile. Inky actually uses both Pac-Man's
         position/facing as well as Blinky's position in his
         calculation. To locate Inky's target, we first start by selecting the
         position two tiles in front of Pac-Man in his current direction of
         travel, similar to Pinky's targeting method. From there, imagine
         drawing a vector from Blinky's position to this tile, and then doubling
         the length of the vector. The tile that this new, extended vector ends
         on will be Inky's actual target.
         Note that Inky's "two tiles in front of Pac-Man" calculation suffers
         from exactly the same overflow error as Pinky's four-tile equivalent,
         so if Pac-Man is heading upwards, the endpoint of the initial vector
         from Blinky (before doubling) will actually be two tiles up and two
         tiles left of Pac-Man.
        */
        std::pair<size_t, size_t> init_pos;
        switch (pacman.direction) {
        case UP: {
          // 2 tiles up and 2 tiles left from pacman
          init_pos.first = pacman.pos.first - 2;
          init_pos.second = pacman.pos.second - 2;
          break;
        }
        // if pacman is facing down, left or right, then 2 tiles ahead of it
        case DOWN:
          init_pos.first = pacman.pos.first;
          init_pos.second = pacman.pos.second + 2;
          break;
        case LEFT:
          init_pos.first = pacman.pos.first - 2;
          init_pos.second = pacman.pos.second;
          break;
        case RIGHT:
          init_pos.first = pacman.pos.first + 2;
          init_pos.second = pacman.pos.second;
          break;
        default:
          break;
        }

        // calculate distance of blinky from 2 tiles ahead of pacman and
        // multiply it by two
        std::pair<size_t, size_t> finale;
        finale.first = std::abs((int)blinky.pos.first - (int)init_pos.first);
        finale.second = std::abs((int)blinky.pos.second - (int)init_pos.second);
        target.first = init_pos.first + finale.first;
        target.second = init_pos.second + finale.second;
        break;
      }
      case CLYDE:
        /*
         Whenever Clyde needs to determine his target tile, he first calculates
         his distance from Pac-Man. If he is farther than eight tiles away, his
         targeting is identical to Blinky's, using Pac-Man's current tile as his
         target. However, as soon as his distance to Pac-Man becomes less than
         eight tiles, Clyde's target is set to the same tile as his fixed one in
         Scatter mode, just outside the bottom-left corner of the maze.
        */
        break;
        if (manhattanDistance(this->pos, pacman.pos) > 8) {
          this->target = pacman.pos;
        } else {
          this->target = {height - 2, 1};
        }
      }
    case FRIGHTENED:
      /*
       Frightened mode is unique because the ghosts do not have a specific
       target tile while in this mode. Instead, they pseudorandomly decide which
       turns to make at every intersection. A ghost in Frightened mode also
       moves much more slowly and can be eaten by Pac-Man.
       */
      break;
    default:
      break;
    }
  }

  static bool has_no_collision(char c) {
    return std::string(" .@~<>v^o").find(c) != std::string::npos;
  }
  // Define a function to check if a position is valid on the game map
  bool isValidPosition(size_t x, size_t y, size_t width, size_t height) {
    return x >= 0 && x < width && y >= 0 && y < height;
  }

  template <size_t width, size_t height>
  void move(const std::array<std::array<char, width>, height> &game_map,
            const player &pacman, const enemy &blinky) {

    // Calculate distance to target for each possible move
    std::vector<std::pair<size_t, size_t>> directions = {
        {0, -1}, {0, 1}, {-1, 0}, {1, 0}};

    // up // down // left // right
    auto getDirection = [](const std::pair<size_t, size_t> &p) {
      if (p == std::pair<size_t, size_t>{0, -1}) {
        return UP;
      }
      if (p == std::pair<size_t, size_t>{0, 1}) {
        return DOWN;
      }
      if (p == std::pair<size_t, size_t>{1, 0}) {
        return RIGHT;
      }
      if (p == std::pair<size_t, size_t>{-1, 0}) {
        return LEFT;
      }
      return LEFT; // don't worry
    };
    auto getOppositeDirection = [](DIRECTION d) {
      switch (d) {
      case LEFT:
        return RIGHT;
        break;
      case RIGHT:
        return LEFT;
        break;
      case UP:
        return DOWN;
        break;
      case DOWN:
        return UP;
        break;
      default:
        return LEFT;
        break;
      }
    };

    // Check possible moves and calculate the shortest path length using
    // Manhattan distance
    size_t currentX = pos.first;
    size_t currentY = pos.second;
    size_t shortestPathLength = UINT_MAX;
    std::pair<size_t, size_t> nextPos = pos;
    DIRECTION new_move = LEFT;

    if (this->mode == ENEMY_MODE::FRIGHTENED) {

      // Define a random number generator
      std::uniform_int_distribution<> dis(
          0, directions.size() -
                 1); // Assuming 'directions' is a vector of possible directions

      bool valid_move_found = false;
      std::array<bool, 4> visited = {0, 0, 0, 0};
      while (!valid_move_found) {
        // Randomly select a direction
        int random_index = dis(gen);
        visited[random_index] = true;
        std::pair<size_t, size_t> selected_direction = directions[random_index];

        // Check if the selected direction is valid and not the opposite of the
        // previous move
        if (isValidPosition(currentX + selected_direction.first,
                            currentY + selected_direction.second, width,
                            height) &&
            has_no_collision(game_map[currentX + selected_direction.first][

                currentY + selected_direction.second]) and
            getDirection(selected_direction) !=
                getOppositeDirection(prev_move)) {
          // Update the ghost's position
          nextPos.first = pos.first + selected_direction.first;
          nextPos.second = pos.second + selected_direction.second;
          new_move = getDirection(selected_direction);
          valid_move_found = true;
          break;
        }

        if (visited[0] and visited[1] and visited[2] and visited[3]) {
          nextPos.first = pos.first;
          nextPos.second = pos.second;
          // There is no way out
          break;
        }
      }
      // Update the player's position
      prev_move = new_move;
      pos = nextPos;
      return;
    }
    calculate_target(width, height, pacman, blinky);

    for (const auto &dir : directions) {
      size_t newX = currentX + dir.first;
      size_t newY = currentY + dir.second;

      if (isValidPosition(newX, newY, width, height) and
          has_no_collision(game_map[newX][newY]) and
          getDirection(dir) != getOppositeDirection(prev_move)) {
        size_t pathLength = manhattanDistance({newX, newY}, target);
        if (pathLength < shortestPathLength) {
          shortestPathLength = pathLength;
          nextPos = {newX, newY};
          new_move = getDirection(dir);
        }
      }
    }

    // Update the player's position
    prev_move = new_move;
    pos = nextPos;
  }
};

std::vector<std::string> get_map_str(const std::string &map_path,
                                     player &player_) {
  std::ifstream map_file(map_path);
  if (!map_file.is_open()) {
    throw std::invalid_argument("File does not exist:" + map_path);
  }

  std::vector<std::string> answer;
  std::string buff;
  const std::set<char> valid_chars = {'#', ' ', '*', '|', '-',
                                      '~', '.', '[', ']', '@'};
  while (std::getline(map_file, buff)) {
    std::string line = "";
    for (size_t i = 0; i < buff.size(); i++) {
      if (valid_chars.find(buff[i]) != valid_chars.end())
        line += buff[i];
      if (buff[i] == '.')
        player_.max_score += 10;
      else if (buff[i] == '@')
        player_.max_score += 50;
      else if (buff[i] == '[')
        player_.portal_2 = {answer.size(), i};
      else if (buff[i] == ']')
        player_.portal_1 = {answer.size(), i};
    }
    answer.push_back(line);
  }

  return answer;
}

template <size_t width, size_t height>
void update_map(std::array<std::array<char, width>, height> &game_map,
                player &player_, std::vector<std::string> &game_vec, enemy &g1,
                enemy &g2, enemy &g3, enemy &g4, int &frightened_countdown) {

  for (int i = 0; i < game_map.size(); i++) {
    for (int j = 0; j < game_map[i].size(); j++) {
      game_map[i][j] = ' ';
    }
  }

  std::string buff;
  int i = 0;
  const std::set<char> valid_chars = {'#', ' ', '*', '|', '-',
                                      '~', '.', '[', ']'};

  for (size_t i = 0; i < game_vec.size(); i++) {
    for (size_t j = 0; j < game_map[i].size() and j < game_vec[i].size(); j++) {
      game_map[i][j] = game_vec[i][j];
    }
  }

  if (game_map[player_.pos.first][player_.pos.second] == '.') {
    game_vec[player_.pos.first][player_.pos.second] = ' ';
    player_.score += 10;
  } else if (game_map[player_.pos.first][player_.pos.second] == '@') {
    game_vec[player_.pos.first][player_.pos.second] = ' ';
    player_.score += 50;
    frightened_countdown = 10;
  } else if (player_.pos.first == player_.portal_1.first and
             player_.pos.second == player_.portal_1.second) {
    player_.pos.first = player_.portal_2.first;
    player_.pos.second = player_.portal_2.second + 1;
  } else if (player_.pos.first == player_.portal_2.first and
             player_.pos.second == player_.portal_2.second) {
    player_.pos.first = player_.portal_1.first;
    player_.pos.second = player_.portal_1.second - 1;
  }

  game_map[player_.pos.first][player_.pos.second] =
      (player_.current_anim_frame < 3
           ? player_.icons[player_.direction].first
           : player_.icons[player_.direction].second);

  game_map[g1.pos.first][g1.pos.second] = g1.icon;
  game_map[g2.pos.first][g2.pos.second] = g2.icon;
  game_map[g3.pos.first][g3.pos.second] = g3.icon;
  game_map[g4.pos.first][g4.pos.second] = g4.icon;

  if ((g1.pos == player_.pos and g1.mode != FRIGHTENED) or
      (g2.pos == player_.pos and g2.mode != FRIGHTENED) or
      (g3.pos == player_.pos and g3.mode != FRIGHTENED) or
      (g4.pos == player_.pos and g4.mode != FRIGHTENED)) {
    player_.is_over = true;
  }
  if (g1.pos == player_.pos and g1.mode == FRIGHTENED) {
    g1.pos = {8, 16};
    g1.set_character(BLINKY);
    g1.mode = NORMAL;
  }
  if (g2.pos == player_.pos and g2.mode == FRIGHTENED) {
    g2.pos = {8, 16};
    g2.set_character(PINKY);
    g4.mode = NORMAL;
  }
  if (g3.pos == player_.pos and g3.mode == FRIGHTENED) {
    g3.pos = {8, 16};
    g3.set_character(INKY);
    g3.mode = NORMAL;
  }
  if (g4.pos == player_.pos and g4.mode == FRIGHTENED) {
    g4.pos = {8, 16};
    g4.set_character(CLYDE);
    g4.mode = NORMAL;
  }
}

int main() {
  constexpr size_t WIDTH = 32;
  constexpr size_t HEIGHT = 40;
  const std::string MAP_FILE_PATH = "./map.txt";

  std::array<std::array<char, HEIGHT>, WIDTH> game_map;
  player pacman = {.pos = {WIDTH / 2, HEIGHT / 2}, .direction = DIRECTION::UP};
  enemy ghost1 = {.pos = {8, 16}, .mode = ENEMY_MODE::SCATTER};
  enemy ghost2 = {.pos = {10, 14}, .mode = ENEMY_MODE::SCATTER};
  enemy ghost3 = {.pos = {10, 15}, .mode = ENEMY_MODE::SCATTER};
  enemy ghost4 = {.pos = {10, 16}, .mode = ENEMY_MODE::SCATTER};
  ghost1.set_character(ENEMY_TYPE::BLINKY);
  ghost2.set_character(ENEMY_TYPE::PINKY);
  ghost3.set_character(ENEMY_TYPE::INKY);
  ghost4.set_character(ENEMY_TYPE::CLYDE);
  bool game_is_running = true;
  int frameCount = 0;
  int secs = 0;
  int frightened_countdown = 0;

  std::vector<std::string> game_vec = get_map_str(MAP_FILE_PATH, pacman);

  // If you want it faster change second parameter of std::ratio
  using std::chrono::system_clock, std::chrono::duration;
  using frames = duration<int64_t, std::ratio<1, 60>>;

  auto nextFrame = system_clock::now();
  auto lastFrame = nextFrame - frames{1};

  while (game_is_running) {

    std::this_thread::sleep_until(nextFrame);
    lastFrame = nextFrame;
    nextFrame +=
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            frames{1});

    frameCount++;

    // clear_screen(); // Makes screen blink a lot after ghost movement
    // implementations
    for (size_t i = 0; i < HEIGHT; i++) {
      std::cout << '\n';
    }

    if (secs == 7) {
      ghost1.mode = ENEMY_MODE::NORMAL;
      ghost2.mode = ENEMY_MODE::NORMAL;
      ghost3.mode = ENEMY_MODE::NORMAL;
      ghost4.mode = ENEMY_MODE::NORMAL;
    }

    if (frightened_countdown != 0) {
      ghost1.mode = ENEMY_MODE::FRIGHTENED;
      ghost2.mode = ENEMY_MODE::FRIGHTENED;
      ghost3.mode = ENEMY_MODE::FRIGHTENED;
      ghost4.mode = ENEMY_MODE::FRIGHTENED;
      ghost1.icon = 'X';
      ghost2.icon = 'X';
      ghost3.icon = 'X';
      ghost4.icon = 'X';
    }

    if (pacman.score == pacman.max_score) {
      std::cout << "You win!" << '\n';
      if (kbhit()) {
        char key = getchar();
        if (key == 'q') {
          game_is_running = false;
        }
      }
    } else if (pacman.is_over) {
      std::cout << "You lost!" << '\n';
      if (kbhit()) {
        char key = getchar();
        if (key == 'q') {
          game_is_running = false;
        }
      }
    } else {
      if (kbhit()) {
        char key = getchar();
        switch (key) {
        case 'W':
        case 'w':
          pacman.direction = DIRECTION::UP;
          break;
        case 'S':
        case 's':
          pacman.direction = DIRECTION::DOWN;
          break;
        case 'A':
        case 'a':
          pacman.direction = DIRECTION::LEFT;
          break;
        case 'D':
        case 'd':
          pacman.direction = DIRECTION::RIGHT;
          break;
        case 'q':
          game_is_running = false;
          break;
        default:
          break;
        }
      }
    }

    if (frameCount == 60) {
      frameCount = 0;
    }

    if (frameCount % 10 == 0) {
      pacman.move(game_map);
      pacman.current_anim_frame += 1;
      if (pacman.current_anim_frame == 5) {
        pacman.current_anim_frame = 1;
      }
      ghost1.move(game_map, pacman, ghost1);
      ghost2.move(game_map, pacman, ghost1);
      ghost3.move(game_map, pacman, ghost1);
      ghost4.move(game_map, pacman, ghost1);
      if (frameCount == 0 and secs != 7) {
        secs++;
        frameCount = 0;
        if (frightened_countdown != 0) {
          frightened_countdown--;
          if (frightened_countdown == 0) {
            // For reseting icons back
            ghost1.set_character(ENEMY_TYPE::BLINKY);
            ghost2.set_character(ENEMY_TYPE::PINKY);
            ghost3.set_character(ENEMY_TYPE::INKY);
            ghost4.set_character(ENEMY_TYPE::CLYDE);
            ghost1.mode = ENEMY_MODE::NORMAL;
            ghost2.mode = ENEMY_MODE::NORMAL;
            ghost3.mode = ENEMY_MODE::NORMAL;
            ghost4.mode = ENEMY_MODE::NORMAL;
          }
        }
      } else if ((frameCount == 0 or frameCount == 60) and
                 frightened_countdown != 0) {
        frightened_countdown--;
        if (frightened_countdown == 0) {
          // For reseting icons back
          ghost1.set_character(ENEMY_TYPE::BLINKY);
          ghost2.set_character(ENEMY_TYPE::PINKY);
          ghost3.set_character(ENEMY_TYPE::INKY);
          ghost4.set_character(ENEMY_TYPE::CLYDE);
        }
      }
    }

    update_map(game_map, pacman, game_vec, ghost1, ghost2, ghost3, ghost4,
               frightened_countdown);

    for (int i = 0; i < game_map.size(); i++) {
      for (int j = 0; j < game_map[i].size(); j++) {
        std::cout << game_map[i][j];
      }
      std::cout << '\n';
    }
    std::cout << '\n' << "Score: " << pacman.score << '\n';
  }

  return 0;
}
