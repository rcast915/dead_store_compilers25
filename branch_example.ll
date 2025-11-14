define void @branch_example(ptr %p, ptr %q) {
entry:
  store i32 1, ptr %p
  br i1 true, label %if, label %else
if:
  store i32 2, ptr %p
  br label %exit
else:
  store i32 3, ptr %p
  br label %exit
exit:
  %v = load i32, ptr %p
  ret void
}
