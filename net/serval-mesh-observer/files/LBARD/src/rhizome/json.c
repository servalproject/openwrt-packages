
int parse_json_line(char *line,char fields[][8192],int num_fields)
{
  int field_count=0;
  int offset=0;
  if (line[offset]!='[') return -1; else offset++;

  while(line[offset]&&line[offset]!=']') {
    if (field_count>=num_fields) return -2;
    if (line[offset]=='"') {
      // quoted field
      int j=0,i;
      for(i=offset+1;(line[i]!='"')&&(i<8191);i++)
	fields[field_count][j++]=line[i];
      fields[field_count++][j]=0;
      offset=i+1;
    } else {
      // naked field
      int j=0,i;
      for(i=offset;(line[i]!=',')&&(line[i]!=']')&&(i<8191);i++)
	fields[field_count][j++]=line[i];
      fields[field_count++][j]=0;
      if (offset==i) return -4;
      offset=i;
    }
    if (line[offset]&&(line[offset]!=',')&&(line[offset]!=']')) return -3;
    if (line[offset]==',') offset++;
  }
  
  return field_count;
}
