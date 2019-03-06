/*
 * @test /nodynamiccopyright/
 * @summary smoke negative test for datum classes
 * @compile/fail/ref=DatumCanNotDeclaredFieldsWithSameName.out -XDrawDiagnostics DatumCanNotDeclaredFieldsWithSameName.java
 */

public class DatumCanNotDeclaredFieldsWithSameName {
    record D1(int x, int x) { }
}