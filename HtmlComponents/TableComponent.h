#pragma once

#include "../D2DGraphicEngine.h"
#include <memory>
#include <vector>

namespace ge {
namespace TableComponent {

// 테이블 컴포넌트 모듈 (Step 4.5.7): 도구/속성창은 표를 직접 만지지 않고 이 함수들을 거친다.
// 그리기 코드가 아니라 모델 편집 코드이므로 D2D 에 의존하지 않는다.

// rows x cols 표 생성 (전체 크기 w x h, 첫 행은 머리글)
std::unique_ptr<TableNode> Create(int rows, int cols, float w, float h);

// 행/열 삽입 (at = 새 행/열의 인덱스, 0..개수). 삽입 위치 아래/오른쪽 칸 내용은 함께 밀린다
bool InsertRow(TableNode& t, int at);
bool InsertCol(TableNode& t, int at);
// 행/열 삭제. 삭제되는 행/열 안의 칸 내용은 함께 지워진다. 마지막 1행/1열은 지울 수 없다
bool DeleteRow(TableNode& t, int at);
bool DeleteCol(TableNode& t, int at);

// 행 높이 / 열 너비 변경 (뒤쪽 칸 내용이 함께 이동)
bool ResizeRow(TableNode& t, int r, float h);
bool ResizeCol(TableNode& t, int c, float w);

// 행/열 수를 한 번에 맞춤 (끝에서 추가/삭제)
bool SetGridSize(TableNode& t, int rows, int cols);

// 칸 병합 / 분할 (병합 범위는 정규화됨)
bool MergeCells(TableNode& t, int r0, int c0, int r1, int c1);
bool SplitCell(TableNode& t, int r, int c);
// (r,c) 칸을 오른쪽/아래 이웃 칸과 병합 (결과가 직사각형이 아니거나 표 밖이면 false)
bool MergeNeighbor(TableNode& t, int r, int c, bool right);

// 자식 노드가 속한 칸 (원점 기준, 병합이면 앵커 칸). 표 밖이면 false
bool ChildCell(const TableNode& t, const Node& child, int& r, int& c);
// 칸(앵커)에 속한 텍스트 노드들 (병합된 칸이면 덮인 칸의 글자도 포함)
std::vector<TextNode*> CellTexts(const TableNode& t, int r, int c);

} // namespace TableComponent
} // namespace ge
