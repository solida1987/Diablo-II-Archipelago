"""Parser for Diablo II tab-delimited data files (.txt).

These files use tab separators, Windows line endings, and must preserve
exact column counts per row or the game will crash on load.
"""


class D2TxtFile:
    def __init__(self, content: str):
        raw_lines = content.replace("\r\n", "\n").replace("\r", "\n").split("\n")
        # Remove trailing empty lines
        while raw_lines and raw_lines[-1].strip() == "":
            raw_lines.pop()

        if not raw_lines:
            raise ValueError("Empty D2 txt file")

        self.headers = raw_lines[0].split("\t")
        self._col_count = len(self.headers)
        self.rows: list[list[str]] = []
        self._expansion_marker_indices: list[int] = []

        for i, line in enumerate(raw_lines[1:], start=1):
            cols = line.split("\t")
            # Detect "Expansion" marker rows (single cell or first cell is "Expansion")
            if cols[0].strip().lower() == "expansion":
                self._expansion_marker_indices.append(len(self.rows))
            # Pad or trim to match header count
            if len(cols) < self._col_count:
                cols.extend([""] * (self._col_count - len(cols)))
            elif len(cols) > self._col_count:
                cols = cols[: self._col_count]
            self.rows.append(cols)

    @classmethod
    def from_file(cls, path: str) -> "D2TxtFile":
        with open(path, "r", encoding="latin-1") as f:
            return cls(f.read())

    def get_col_index(self, col_name: str) -> int:
        try:
            return self.headers.index(col_name)
        except ValueError:
            raise KeyError(f"Column '{col_name}' not found. Available: {self.headers[:20]}...")

    def get_value(self, row_idx: int, col_name: str) -> str:
        return self.rows[row_idx][self.get_col_index(col_name)]

    def set_value(self, row_idx: int, col_name: str, value: str) -> None:
        self.rows[row_idx][self.get_col_index(col_name)] = value

    def find_rows(self, col_name: str, value: str) -> list[int]:
        ci = self.get_col_index(col_name)
        return [i for i, row in enumerate(self.rows) if row[ci] == value]

    def find_rows_nonempty(self, col_name: str) -> list[int]:
        ci = self.get_col_index(col_name)
        return [i for i, row in enumerate(self.rows) if row[ci].strip() != ""]

    def to_string(self) -> str:
        lines = ["\t".join(self.headers)]
        for row in self.rows:
            # Ensure exact column count
            padded = row[: self._col_count]
            if len(padded) < self._col_count:
                padded.extend([""] * (self._col_count - len(padded)))
            lines.append("\t".join(padded))
        return "\r\n".join(lines) + "\r\n"

    def save(self, path: str) -> None:
        with open(path, "w", encoding="latin-1", newline="") as f:
            f.write(self.to_string())
