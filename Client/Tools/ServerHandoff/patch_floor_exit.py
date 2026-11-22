# 테스트용 사본에서만: fbx_to_map.py 결과에 클라 배치의 floor_exit(문 밖 바닥)를 되살린다.
# 사용: python patch_floor_exit.py <client_layout.json> <converted.json> <out.json>
import json, sys
client = json.load(open(sys.argv[1], encoding='utf-8'))
conv = json.load(open(sys.argv[2], encoding='utf-8'))
ids = {f.get('id') for f in conv['floors']}
added = [f for f in client['floors'] if f.get('id') not in ids]
conv['floors'].extend(added)
json.dump(conv, open(sys.argv[3], 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print('added floors:', [f['id'] for f in added])
print('meta:', json.dumps(conv.get('meta'), ensure_ascii=True))
