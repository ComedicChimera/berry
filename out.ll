; ModuleID = 'main'
source_filename = "main"

%_array = type { ptr, i64 }

@_fltused = constant double 0.000000e+00
@"_br7$0.main.stdout" = private global ptr null, !dbg !0
@0 = private unnamed_addr constant [14 x i8] c"Hello, world!\0A"
@1 = private unnamed_addr constant %_array { ptr @0, i64 14 }

declare win64cc void @ExitProcess(i32)

declare win64cc ptr @GetStdHandle(i32)

declare win64cc i1 @WriteConsoleA(ptr, ptr, i32, ptr, ptr)

define void @__LibBerry_Init() {
entry:
  %0 = call ptr @GetStdHandle(i32 -11)
  store ptr %0, ptr @"_br7$0.main.stdout", align 8
  ret void
}

define void @__LibBerry_Start() !dbg !7 {
entry:
  br label %0

0:                                                ; preds = %entry
  call void @__LibBerry_Init(), !dbg !10
  call void @"_br7$0.main.main"(), !dbg !11
  call void @ExitProcess(i32 0), !dbg !12
  ret void, !dbg !12
}

define void @__LibBerry_Panic() !dbg !13 {
entry:
  br label %0

0:                                                ; preds = %entry
  call void @ExitProcess(i32 1), !dbg !14
  ret void, !dbg !14
}

define private void @"_br7$0.main.putc"(i32 %c) !dbg !15 {
entry:
  %0 = alloca i32, align 4
  store i32 %c, ptr %0, align 4
  %1 = alloca i8, align 1, !dbg !21
  br label %2

2:                                                ; preds = %entry
  call void @llvm.dbg.declare(metadata ptr %1, metadata !20, metadata !DIExpression()), !dbg !21
  %3 = load i32, ptr %0, align 4, !dbg !22
  %4 = trunc i32 %3 to i8, !dbg !22
  store i8 %4, ptr %1, align 1, !dbg !22
  %5 = load ptr, ptr @"_br7$0.main.stdout", align 8, !dbg !23
  %6 = call i1 @WriteConsoleA(ptr %5, ptr %1, i32 1, ptr null, ptr null), !dbg !24
  ret void, !dbg !24
}

define private void @"_br7$0.main.puti"(i64 %n) !dbg !25 {
entry:
  %0 = alloca i64, align 8
  store i64 %n, ptr %0, align 4
  %1 = alloca i64, align 8, !dbg !34
  %2 = alloca i1, align 1, !dbg !35
  %3 = alloca i64, align 8, !dbg !36
  br label %4

4:                                                ; preds = %entry
  %5 = load i64, ptr %0, align 4, !dbg !37
  %6 = icmp eq i64 %5, 0, !dbg !38
  br i1 %6, label %8, label %9, !dbg !38

7:                                                ; preds = %15, %12, %8
  call void @llvm.dbg.declare(metadata ptr %1, metadata !30, metadata !DIExpression()), !dbg !34
  store i64 1000000000000000000, ptr %1, align 4, !dbg !39
  call void @llvm.dbg.declare(metadata ptr %2, metadata !31, metadata !DIExpression()), !dbg !35
  store i1 false, ptr %2, align 1, !dbg !40
  br label %17, !dbg !41

8:                                                ; preds = %4
  call void @"_br7$0.main.putc"(i32 48), !dbg !42
  br label %7, !dbg !42

9:                                                ; preds = %4
  %10 = load i64, ptr %0, align 4, !dbg !43
  %11 = icmp slt i64 %10, 0, !dbg !44
  br i1 %11, label %12, label %15, !dbg !44

12:                                               ; preds = %9
  call void @"_br7$0.main.putc"(i32 45), !dbg !45
  %13 = load i64, ptr %0, align 4, !dbg !46
  %14 = sub i64 0, %13, !dbg !46
  store i64 %14, ptr %0, align 4, !dbg !46
  br label %7, !dbg !46

15:                                               ; preds = %9
  br label %7, !dbg !46

16:                                               ; preds = %17
  ret void, !dbg !47

17:                                               ; preds = %27, %7
  %18 = load i64, ptr %1, align 4, !dbg !48
  %19 = icmp sgt i64 %18, 0, !dbg !49
  br i1 %19, label %20, label %16, !dbg !49

20:                                               ; preds = %17
  call void @llvm.dbg.declare(metadata ptr %3, metadata !33, metadata !DIExpression()), !dbg !36
  %21 = load i64, ptr %0, align 4, !dbg !50
  %22 = load i64, ptr %1, align 4, !dbg !51
  %23 = sdiv i64 %21, %22, !dbg !51
  %24 = srem i64 %23, 10, !dbg !52
  store i64 %24, ptr %3, align 4, !dbg !52
  %25 = load i64, ptr %3, align 4, !dbg !53
  %26 = icmp sgt i64 %25, 0, !dbg !54
  br i1 %26, label %37, label %35, !dbg !54

27:                                               ; preds = %34, %30
  %28 = load i64, ptr %1, align 4, !dbg !55
  %29 = sdiv i64 %28, 10, !dbg !47
  store i64 %29, ptr %1, align 4, !dbg !47
  br label %17, !dbg !47

30:                                               ; preds = %37
  %31 = load i64, ptr %3, align 4, !dbg !56
  %32 = trunc i64 %31 to i32, !dbg !56
  %33 = add i32 48, %32, !dbg !56
  call void @"_br7$0.main.putc"(i32 %33), !dbg !56
  store i1 true, ptr %2, align 1, !dbg !57
  br label %27, !dbg !57

34:                                               ; preds = %37
  br label %27, !dbg !57

35:                                               ; preds = %20
  %36 = load i1, ptr %2, align 1, !dbg !58
  br label %37, !dbg !58

37:                                               ; preds = %35, %20
  %38 = phi i1 [ %26, %20 ], [ %36, %35 ], !dbg !58
  br i1 %38, label %30, label %34, !dbg !58
}

define private void @"_br7$0.main.write"(%_array %b) !dbg !59 {
entry:
  %0 = alloca %_array, align 8
  store %_array %b, ptr %0, align 8
  br label %1

1:                                                ; preds = %entry
  %2 = load ptr, ptr @"_br7$0.main.stdout", align 8, !dbg !63
  %3 = load %_array, ptr %0, align 8, !dbg !64
  %4 = extractvalue %_array %3, 0, !dbg !64
  %5 = load %_array, ptr %0, align 8, !dbg !65
  %6 = extractvalue %_array %5, 1, !dbg !65
  %7 = trunc i64 %6 to i32, !dbg !65
  %8 = call i1 @WriteConsoleA(ptr %2, ptr %4, i32 %7, ptr null, ptr null), !dbg !66
  ret void, !dbg !66
}

define private void @"_br7$0.main.main"() !dbg !67 {
entry:
  %0 = alloca %_array, align 8, !dbg !70
  %1 = alloca i8, i64 4, align 1, !dbg !71
  br label %2

2:                                                ; preds = %entry
  call void @llvm.dbg.declare(metadata ptr %0, metadata !69, metadata !DIExpression()), !dbg !70
  %3 = getelementptr inbounds [4 x i8], ptr %1, i32 0, i64 0, !dbg !71
  store i8 72, ptr %3, align 1, !dbg !72
  %4 = getelementptr inbounds [4 x i8], ptr %1, i32 0, i64 1, !dbg !72
  store i8 101, ptr %4, align 1, !dbg !73
  %5 = getelementptr inbounds [4 x i8], ptr %1, i32 0, i64 2, !dbg !73
  store i8 33, ptr %5, align 1, !dbg !74
  %6 = getelementptr inbounds [4 x i8], ptr %1, i32 0, i64 3, !dbg !74
  store i8 10, ptr %6, align 1, !dbg !75
  %7 = getelementptr inbounds %_array, ptr %0, i32 0, i32 0, !dbg !75
  store ptr %1, ptr %7, align 8, !dbg !75
  %8 = getelementptr inbounds %_array, ptr %0, i32 0, i32 1, !dbg !75
  store i64 4, ptr %8, align 4, !dbg !75
  %9 = load %_array, ptr %0, align 8, !dbg !76
  call void @"_br7$0.main.write"(%_array %9), !dbg !76
  call void @"_br7$0.main.write"(%_array { ptr @1, i64 14 }), !dbg !77
  ret void, !dbg !77
}

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare void @llvm.dbg.declare(metadata, metadata, metadata) #0

attributes #0 = { nocallback nofree nosync nounwind speculatable willreturn memory(none) }

!llvm.dbg.cu = !{!5}

!0 = !DIGlobalVariableExpression(var: !1, expr: !DIExpression())
!1 = distinct !DIGlobalVariable(name: "stdout", linkageName: "private", scope: !2, file: !2, line: 29, type: !3, isLocal: true, isDefinition: true)
!2 = !DIFile(filename: "hello2.bry", directory: "tests/v3")
!3 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !4, size: 64)
!4 = !DIBasicType(name: "u8", size: 8, encoding: DW_ATE_unsigned)
!5 = distinct !DICompileUnit(language: DW_LANG_C, file: !2, producer: "berryc-v0.0.1", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, globals: !6)
!6 = !{!0}
!7 = distinct !DISubprogram(name: "__LibBerry_Start", linkageName: "external", scope: !2, file: !2, line: 16, type: !8, scopeLine: 16, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !5)
!8 = !DISubroutineType(cc: DW_CC_normal, types: !9)
!9 = !{null}
!10 = !DILocation(line: 17, column: 5, scope: !7)
!11 = !DILocation(line: 19, column: 5, scope: !7)
!12 = !DILocation(line: 21, column: 17, scope: !7)
!13 = distinct !DISubprogram(name: "__LibBerry_Panic", linkageName: "external", scope: !2, file: !2, line: 25, type: !8, scopeLine: 25, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !5)
!14 = !DILocation(line: 26, column: 17, scope: !13)
!15 = distinct !DISubprogram(name: "putc", linkageName: "private", scope: !2, file: !2, line: 32, type: !16, scopeLine: 32, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !5, retainedNodes: !19)
!16 = !DISubroutineType(cc: DW_CC_normal, types: !17)
!17 = !{null, !18}
!18 = !DIBasicType(name: "i32", size: 32, encoding: DW_ATE_signed)
!19 = !{!20}
!20 = !DILocalVariable(name: "b", scope: !15, file: !2, line: 33, type: !4)
!21 = !DILocation(line: 33, column: 5, scope: !15)
!22 = !DILocation(line: 33, column: 13, scope: !15)
!23 = !DILocation(line: 34, column: 19, scope: !15)
!24 = !DILocation(line: 34, column: 40, scope: !15)
!25 = distinct !DISubprogram(name: "puti", linkageName: "private", scope: !2, file: !2, line: 39, type: !26, scopeLine: 39, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !5, retainedNodes: !29)
!26 = !DISubroutineType(cc: DW_CC_normal, types: !27)
!27 = !{null, !28}
!28 = !DIBasicType(name: "i64", size: 64, encoding: DW_ATE_signed)
!29 = !{!30, !31, !33}
!30 = !DILocalVariable(name: "place", scope: !25, file: !2, line: 47, type: !28)
!31 = !DILocalVariable(name: "hit_nonzero", scope: !25, file: !2, line: 48, type: !32)
!32 = !DIBasicType(name: "bool", size: 1, encoding: DW_ATE_boolean)
!33 = !DILocalVariable(name: "digit", scope: !25, file: !2, line: 50, type: !28)
!34 = !DILocation(line: 47, column: 5, scope: !25)
!35 = !DILocation(line: 48, column: 5, scope: !25)
!36 = !DILocation(line: 50, column: 9, scope: !25)
!37 = !DILocation(line: 40, column: 8, scope: !25)
!38 = !DILocation(line: 40, column: 13, scope: !25)
!39 = !DILocation(line: 47, column: 17, scope: !25)
!40 = !DILocation(line: 48, column: 23, scope: !25)
!41 = !DILocation(line: 49, column: 5, scope: !25)
!42 = !DILocation(line: 41, column: 14, scope: !25)
!43 = !DILocation(line: 42, column: 12, scope: !25)
!44 = !DILocation(line: 42, column: 16, scope: !25)
!45 = !DILocation(line: 43, column: 14, scope: !25)
!46 = !DILocation(line: 44, column: 14, scope: !25)
!47 = !DILocation(line: 57, column: 18, scope: !25)
!48 = !DILocation(line: 49, column: 11, scope: !25)
!49 = !DILocation(line: 49, column: 19, scope: !25)
!50 = !DILocation(line: 50, column: 22, scope: !25)
!51 = !DILocation(line: 50, column: 26, scope: !25)
!52 = !DILocation(line: 50, column: 35, scope: !25)
!53 = !DILocation(line: 52, column: 12, scope: !25)
!54 = !DILocation(line: 52, column: 20, scope: !25)
!55 = !DILocation(line: 57, column: 9, scope: !25)
!56 = !DILocation(line: 53, column: 25, scope: !25)
!57 = !DILocation(line: 54, column: 27, scope: !25)
!58 = !DILocation(line: 52, column: 25, scope: !25)
!59 = distinct !DISubprogram(name: "write", linkageName: "private", scope: !2, file: !2, line: 61, type: !60, scopeLine: 61, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !5)
!60 = !DISubroutineType(cc: DW_CC_normal, types: !61)
!61 = !{null, !62}
!62 = !DIBasicType(name: "i8", size: 8, encoding: DW_ATE_signed)
!63 = !DILocation(line: 62, column: 19, scope: !59)
!64 = !DILocation(line: 62, column: 27, scope: !59)
!65 = !DILocation(line: 62, column: 35, scope: !59)
!66 = !DILocation(line: 62, column: 56, scope: !59)
!67 = distinct !DISubprogram(name: "main", linkageName: "private", scope: !2, file: !2, line: 65, type: !8, scopeLine: 65, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !5, retainedNodes: !68)
!68 = !{!69}
!69 = !DILocalVariable(name: "arr", scope: !67, file: !2, line: 66, type: !62)
!70 = !DILocation(line: 66, column: 5, scope: !67)
!71 = !DILocation(line: 66, column: 15, scope: !67)
!72 = !DILocation(line: 66, column: 16, scope: !67)
!73 = !DILocation(line: 66, column: 27, scope: !67)
!74 = !DILocation(line: 66, column: 38, scope: !67)
!75 = !DILocation(line: 66, column: 49, scope: !67)
!76 = !DILocation(line: 67, column: 11, scope: !67)
!77 = !DILocation(line: 69, column: 11, scope: !67)
